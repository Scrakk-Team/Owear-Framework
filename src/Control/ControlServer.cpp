// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/ControlServer.cpp — lógica común de comandos.
// Transporte por plataforma en ControlServer_<platform>.cpp.
//
#include "ControlServer.hpp"

#include "../Bridge/Dispatcher.hpp"
#include "../Core/App.hpp"
#include "../Protocol/ProtocolRegistry.hpp"
#include "../Session/PermissionBroker.hpp"
#include "../Session/WebRequestBroker.hpp"
#include "../Session/WindowOpenBroker.hpp"
#include "../Window/Window_p.hpp"
#include "../Core/Log.hpp"
#include "../Runtime/NodeManager.hpp"
#include "ow/App.h"
#include "ow/Window.h"
#include "ow/Base64.h"
#include "ow/Shm.h"
#include "ow/detail/minjson.hpp"

#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ow {

using V = json::Value;

uint32_t CurrentPid() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

std::map<WindowId, Window*>& LiveWindows() {
    static std::map<WindowId, Window*> m;
    return m;
}

// ControlServer::Get() se define por plataforma (ControlServer_<plat>.cpp)
// porque devuelve la subclase concreta con el transporte.

void ControlServer::SendLine(uint64_t clientId, std::string_view line) {
    PlatformSend(clientId, line);
}

void ControlServer::SendResponse(uint64_t clientId, uint64_t id, bool ok,
                                 std::string_view resultJson, std::string_view error) {
    std::string out = "{\"id\":" + std::to_string(id) + ",\"ok\":";
    out += ok ? "true" : "false";
    if (ok) {
        out += ",\"result\":";
        out.append(resultJson.empty() ? "null" : resultJson);
    } else {
        out += ",\"error\":";
        out += json::Value(std::string(error)).Serialize();
    }
    out += "}";
    SendLine(clientId, out);
}

void ControlServer::BroadcastEvent(const std::string& name, std::string_view paramsJson) {
    std::string line = "{\"event\":";
    line += json::Value(name).Serialize();
    line += ",\"params\":";
    line.append(paramsJson.empty() ? "null" : paramsJson);
    line += "}";
    SendLine(0, line);
}

void ControlServer::ForwardNodeCall(WindowId windowId, uint64_t invokeId,
                                    const std::string& fn,
                                    std::string_view argsJson) {
    const uint64_t reqId = nextNodeReqId_++;
    pendingNodeCalls_[reqId] = PendingNodeCall{windowId, invokeId};

    json::Object p;
    p.emplace_back("reqId", json::Value(static_cast<int64_t>(reqId)));
    p.emplace_back("fn", json::Value(fn));
    // Ventana de origen: el handler del main la recibe como contexto (app.handleContext).
    p.emplace_back("windowId", json::Value(static_cast<int64_t>(windowId)));
    auto parsed = json::Parse(argsJson);
    p.emplace_back("args",
                   parsed.value ? std::move(*parsed.value) : json::Value(nullptr));
    BroadcastEvent("node.request", json::Value(std::move(p)).Serialize());
}

void ControlServer::HandleClientDisconnected(uint64_t) {}

void ControlServer::WireWindowEvents(WindowId id, Window* w) {
    auto forward = [this, id](const std::string& name) {
        return [this, id, name](std::string_view payload) {
            json::Object params;
            params.emplace_back("windowId", json::Value(static_cast<int64_t>(id)));
            params.emplace_back("name", json::Value(name));
            params.emplace_back(
                "payload",
                json::Value(json::Value(nullptr))); // placeholder reemplazado abajo
            // parsea payload para incrustarlo tal cual
            json::Value pv = json::Value(nullptr);
            if (auto p = json::Parse(payload); p.value) pv = std::move(*p.value);
            params.back().second = std::move(pv);
            BroadcastEvent("window.event",
                           json::Value(std::move(params)).Serialize());
        };
    };

    w->On("resize", forward("resize"));
    w->On("move", forward("move"));
    w->On("focus", forward("focus"));
    w->On("blur", forward("blur"));
    w->On("maximize", forward("maximize"));
    w->On("unmaximize", forward("unmaximize"));
    w->On("enterFullScreen", forward("enterFullScreen"));
    w->On("leaveFullScreen", forward("leaveFullScreen"));
    // F3.4: el SDK también recibe closeRequested (con requestId) para poder
    // vetar desde el proceso principal: win.on('closeRequested') + closeRespond.
    // El comentario de abajo lo daba por hecho, pero nunca se conectaba: el
    // main no se enteraba y todo cierre desde el SDK acababa en el timeout.
    w->On("closeRequested", forward("closeRequested"));
    // navegación (F-next)
    w->On("navigationStarted", forward("navigationStarted"));
    w->On("loadCommitted", forward("loadCommitted"));
    w->On("didFinishLoad", forward("didFinishLoad"));
    w->On("didFailLoad", forward("didFailLoad"));
    w->On("pageTitleUpdated", forward("pageTitleUpdated"));
    w->On("beforeInput", forward("beforeInput"));
    // F3.4: closeRequested se reenvía al SDK con requestId; el kernel
    // decide con window.respondCloseRequest o el timeout
    // OW_CLOSE_TIMEOUT_MS (default 1000 ms).
    w->On("closed", [this, id](std::string_view) {
        auto it = LiveWindows().find(id);
        if (it == LiveWindows().end()) return;
        Window* dead = it->second;
        LiveWindows().erase(it);
        json::Object params;
        params.emplace_back("windowId", json::Value(static_cast<int64_t>(id)));
        params.emplace_back("name", json::Value("closed"));
        params.emplace_back("payload", json::Value(nullptr));
        BroadcastEvent("window.event", json::Value(std::move(params)).Serialize());
        // app.on('window-all-closed') cuando se cierra la última ventana.
        if (LiveWindows().empty())
            BroadcastEvent("app.event", R"({"name":"window-all-closed","payload":null})");
        // destruye el objeto C++ fuera del signal handler de GTK
        App::Post([dead] { delete dead; });
    });
}

bool ControlServer::Start() {
    if (started_) return true;
    if (!PlatformListen()) return false;
    started_ = true;
    return true;
}

void ControlServer::Stop() {
    if (!started_) return;
    PlatformStop();
    started_ = false;
}

std::string ControlServer::SocketPath() const { return socketPath_; }

namespace {
json::Object ModuleInfoJson(const Dispatcher::ModuleInfo& m) {
    json::Object o;
    o.emplace_back("name", V(m.name));
    o.emplace_back("version", V(m.version));
    o.emplace_back("origin", V(m.origin));
    o.emplace_back("builtin", V(m.builtin));
    o.emplace_back("functions", V(static_cast<int64_t>(m.functions.size())));
    json::Array names;
    for (const auto& fn : m.functions) names.emplace_back(V(fn));
    o.emplace_back("functionNames", V(std::move(names)));
    return o;
}
} // namespace

bool ControlServer::HandleCommand(uint64_t clientId, uint64_t id,
                                  const std::string& cmd,
                                  std::string_view paramsJson,
                                  std::string& resultJson, std::string& error) {
    auto parsed = json::Parse(paramsJson);
    V params = parsed.value ? std::move(*parsed.value) : V(json::Object{});

    auto getWindow = [&](Window** out) -> bool {
        const V* wid = params.IsArray() ? (params.AsArray().empty() ? nullptr : &params.AsArray()[0])
                                        : params.Find("windowId");
        if (!wid || !wid->IsNumber()) { error = "windowId requerido"; return false; }
        auto it = LiveWindows().find(static_cast<WindowId>(wid->AsInt()));
        if (it == LiveWindows().end()) { error = "ventana no encontrada"; return false; }
        *out = it->second;
        return true;
    };

    if (cmd == "app.info") {
        json::Object o;
        o.emplace_back("pid", V(CurrentPid()));
        o.emplace_back("version", V(OW_VERSION_STRING));
        o.emplace_back("socket", V(socketPath_));
        o.emplace_back("name", V(internal::AppName()));

        const char* mainEnv = std::getenv("OW_APP_MAIN");
        const char* assetsEnv = std::getenv("OW_ASSETS_DIR");
        std::string appPath;
        if (mainEnv && *mainEnv)
            appPath = std::filesystem::path(mainEnv).parent_path().string();
        else if (assetsEnv && *assetsEnv)
            appPath = assetsEnv;
        else {
            std::error_code ec;
            appPath = std::filesystem::current_path(ec).string();
        }
        o.emplace_back("appPath", V(appPath));
        o.emplace_back("exePath", V(internal::ExecutableDir()));
        const char* packagedEnv = std::getenv("OW_PACKAGED");
        const bool packaged =
            (packagedEnv && std::string(packagedEnv) == "1") ||
            ((!mainEnv || !*mainEnv) && assetsEnv && *assetsEnv);
        o.emplace_back("packaged", V(packaged));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }
    if (cmd == "app.setName") {
        const V* n = params.Find("name");
        if (!n || !n->IsString()) { error = "name requerido"; return false; }
        internal::SetAppName(n->AsString());
        resultJson = "null";
        return true;
    }
    if (cmd == "app.commandLine.appendSwitch") {
        const V* k = params.Find("key");
        if (!k || !k->IsString()) { error = "key requerido"; return false; }
        std::string arg = "--" + k->AsString();
        if (const V* v = params.Find("value"); v && v->IsString() && !v->AsString().empty())
            arg += "=" + v->AsString();
        internal::AppendCommandArg(arg);
        resultJson = "null";
        return true;
    }
    if (cmd == "app.commandLine.appendArgument") {
        const V* a = params.Find("arg");
        if (!a || !a->IsString()) { error = "arg requerido"; return false; }
        internal::AppendCommandArg(a->AsString());
        resultJson = "null";
        return true;
    }

    if (cmd == "app.quit") {
        // Responde PRIMERO y pide el quit DESPUÉS (con margen): si Quit
        // postea WM_QUIT inmediatamente, el main loop sale y Stop cierra
        // la pipe antes de que la respuesta llegue al cliente (mordido).
        // Sin Post inmediato: solo el delay — la respuesta sale por el
        // outbox durante la ventana de gracia.
        resultJson = "null";
        internal::PlatformDelay(300, [] { App::Quit(0); });
        return true;
    }

    if (cmd == "node.ensure") {
        std::string range = "latest";
        if (const V* r = params.Find("range"); r && r->IsString()) range = r->AsString();
        auto node = NodeManager::Resolve(range);
        if (node.IsErr()) { error = node.Error(); return false; }
        json::Object o;
        o.emplace_back("path", V(node.Value().bin.string()));
        o.emplace_back("version", V(node.Value().version));
        o.emplace_back("source", V(node.Value().source));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }

    // ── módulos nativos desde el proceso principal (Node) ────────────────
    // Esta es la pieza que hace la API "tipo Electron": el main puede usar
    // fs, dialog, tray, menu… igual que el renderer, sin depender de una
    // ventana. Antes sólo existía el camino del bridge (renderer → módulo).
    if (cmd == "module.invoke") {
        const V* mod = params.Find("module");
        const V* fn = params.Find("method");
        if (!mod || !mod->IsString()) { error = "module requerido"; return false; }
        if (!fn || !fn->IsString()) { error = "method requerido"; return false; }

        // Los args viajan como array JSON, igual que desde el bridge.
        std::string argsJson = "[]";
        if (const V* a = params.Find("args"); a) argsJson = a->Serialize();

        uint32_t winId = 0;
        if (const V* wid = params.Find("windowId"); wid && wid->IsNumber())
            winId = static_cast<uint32_t>(wid->AsInt());

        ow_request_t req{};
        req.json = argsJson.c_str();
        req.json_len = static_cast<uint32_t>(argsJson.size());
        req.window_id = winId;

        ow_response_t res{};
        Dispatcher::Get().Execute(winId, mod->AsString(), fn->AsString(), &req, &res);
        if (res.status != 0) {
            error = res.error ? std::string(res.error) : std::string("error en el módulo");
            return false;
        }
        // Contrato ABI: el buffer sólo vive durante la llamada → se copia ya.
        resultJson.assign(res.json ? res.json : "null", res.json_len);
        return true;
    }

    if (cmd == "module.list") {
        json::Array arr;
        for (const auto& m : Dispatcher::Get().Modules())
            arr.emplace_back(V(ModuleInfoJson(m)));
        resultJson = V(std::move(arr)).Serialize();
        return true;
    }

    if (cmd == "module.info") {
        std::string name;
        if (const V* n = params.Find("name"); n && n->IsString()) name = n->AsString();
        Dispatcher::ModuleInfo m;
        if (name.empty() || !Dispatcher::Get().Module(name, m)) {
            error = "módulo desconocido: " + name;
            return false;
        }
        resultJson = V(ModuleInfoJson(m)).Serialize();
        return true;
    }

    if (cmd == "window.create") {
        WindowOptions opts;
        if (const V* t = params.Find("title"); t && t->IsString()) opts.title = t->AsString();
        if (const V* v = params.Find("width"); v && v->IsNumber()) opts.width = (int)v->AsInt();
        if (const V* v = params.Find("height"); v && v->IsNumber()) opts.height = (int)v->AsInt();
        if (const V* v = params.Find("x"); v && v->IsNumber()) opts.center = false;
        if (const V* v = params.Find("resizable"); v && v->IsBool()) opts.resizable = v->AsBool();
        if (const V* v = params.Find("frameless"); v && v->IsBool()) opts.frameless = v->AsBool();
        if (opts.frameless) opts.titleBarStyle = TitleBarStyle::Hidden;
        if (const V* v = params.Find("titleBarStyle"); v && v->IsString()) {
            std::string s = v->AsString();
            if (s == "hidden") opts.titleBarStyle = TitleBarStyle::Hidden;
            else if (s == "custom") opts.titleBarStyle = TitleBarStyle::Custom;
            else opts.titleBarStyle = TitleBarStyle::Default;
        }
        if (const V* v = params.Find("titleBarOverlay")) {
            TitleBarOverlay ov;
            if (v->IsBool()) {
                ov.enabled = v->AsBool();
            } else if (v->IsObject()) {
                ov.enabled = true; // presente ⇒ activo (estilo Electron)
                if (const V* e = v->Find("enabled"); e && e->IsBool())
                    ov.enabled = e->AsBool();
                if (const V* c = v->Find("color"); c && c->IsString())
                    ov.color = c->AsString();
                if (const V* sc = v->Find("symbolColor"); sc && sc->IsString())
                    ov.symbolColor = sc->AsString();
                if (const V* bc = v->Find("buttonColor"); bc && bc->IsString())
                    ov.buttonColor = bc->AsString();
                if (const V* h = v->Find("height"); h && h->IsNumber())
                    ov.height = static_cast<int>(h->AsInt());
            }
            opts.titleBarOverlay = ov;
        }
        // titleBarOverlay implica titlebar custom (como Electron con hidden).
        if (opts.titleBarOverlay.enabled &&
            opts.titleBarStyle == TitleBarStyle::Default)
            opts.titleBarStyle = TitleBarStyle::Custom;
        if (const V* v = params.Find("url"); v && v->IsString()) opts.url = v->AsString();
        if (const V* v = params.Find("session"); v && v->IsString()) opts.session = v->AsString();

        auto* win = new Window(opts);
        WindowId wid = win->Id();
        LiveWindows()[wid] = win;
        WireWindowEvents(wid, win);

        json::Object o;
        o.emplace_back("windowId", V(static_cast<int64_t>(wid)));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }

    Window* w = nullptr;
    if (cmd.rfind("window.", 0) == 0 && cmd != "window.create") {
        if (!getWindow(&w)) return false;
    }

    if (cmd == "window.close") { w->Close(); resultJson = "null"; return true; }
    if (cmd == "window.destroy") {
        // destroy dispara 'closed' → limpia LiveWindows
        w->Destroy();
        resultJson = "null";
        return true;
    }
    if (cmd == "window.show") { w->Show(); resultJson = "null"; return true; }
    if (cmd == "window.hide") { w->Hide(); resultJson = "null"; return true; }
    if (cmd == "window.focus") { w->Focus(); resultJson = "null"; return true; }
    if (cmd == "window.minimize") { w->Minimize(); resultJson = "null"; return true; }
    if (cmd == "window.maximize") {
        bool target = true;
        if (const V* v = params.Find("enabled"); v && v->IsBool()) target = v->AsBool();
        target ? w->Maximize() : w->Unmaximize();
        resultJson = "null";
        return true;
    }
    if (cmd == "window.unmaximize") { w->Unmaximize(); resultJson = "null"; return true; }
    if (cmd == "window.setFullScreen") {
        bool enabled = false;
        if (const V* v = params.Find("enabled"); v && v->IsBool()) enabled = v->AsBool();
        w->SetFullScreen(enabled);
        resultJson = "null";
        return true;
    }
    if (cmd == "window.isMaximized") {
        resultJson = w->IsMaximized() ? "true" : "false";
        return true;
    }
    if (cmd == "window.isMinimized") {
        resultJson = w->IsMinimized() ? "true" : "false";
        return true;
    }
    if (cmd == "window.getBounds") {
        auto b = w->GetBounds();
        json::Object o;
        o.emplace_back("x", V(b.x));
        o.emplace_back("y", V(b.y));
        o.emplace_back("width", V(b.w));
        o.emplace_back("height", V(b.h));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }
    if (cmd == "window.setBounds") {
        Window::Bounds b{};
        if (const V* v = params.Find("x"); v && v->IsNumber()) b.x = (int)v->AsInt();
        if (const V* v = params.Find("y"); v && v->IsNumber()) b.y = (int)v->AsInt();
        if (const V* v = params.Find("width"); v && v->IsNumber()) b.w = (int)v->AsInt();
        if (const V* v = params.Find("height"); v && v->IsNumber()) b.h = (int)v->AsInt();
        w->SetBounds(b);
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setTitleBarOverlay") {
        TitleBarOverlay ov;
        if (const V* v = params.Find("titleBarOverlay")) {
            if (v->IsBool()) {
                ov.enabled = v->AsBool();
            } else if (v->IsObject()) {
                ov.enabled = true;
                if (const V* e = v->Find("enabled"); e && e->IsBool())
                    ov.enabled = e->AsBool();
                if (const V* c = v->Find("color"); c && c->IsString())
                    ov.color = c->AsString();
                if (const V* sc = v->Find("symbolColor"); sc && sc->IsString())
                    ov.symbolColor = sc->AsString();
                if (const V* bc = v->Find("buttonColor"); bc && bc->IsString())
                    ov.buttonColor = bc->AsString();
                if (const V* h = v->Find("height"); h && h->IsNumber())
                    ov.height = static_cast<int>(h->AsInt());
            }
        } else {
            ov.enabled = true; // sin payload ⇒ activar
        }
        w->SetTitleBarOverlay(ov);
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setTitle") {
        const V* t = params.Find("title");
        if (!t || !t->IsString()) { error = "title requerido"; return false; }
        w->SetTitle(t->AsString());
        resultJson = "null";
        return true;
    }
    if (cmd == "window.loadURL") {
        const V* u = params.Find("url");
        if (!u || !u->IsString()) { error = "url requerida"; return false; }
        w->LoadURL(u->AsString());
        resultJson = "null";
        return true;
    }
    if (cmd == "window.respondCloseRequest") {
        const V* rid = params.Find("requestId");
        const V* allow = params.Find("allow");
        if (!rid || !rid->IsNumber() || !allow || !allow->IsBool()) {
            error = "requestId y allow requeridos";
            return false;
        }
        w->impl()->RespondJsClose(static_cast<uint64_t>(rid->AsInt()),
                                  allow->AsBool());
        resultJson = "null";
        return true;
    }
    // ── theme: forzar el esquema de color del contenido ──────────────────
    if (cmd == "window.setColorScheme") {
        const V* s = params.Find("scheme");
        const int mode = (s && s->IsNumber()) ? static_cast<int>(s->AsInt()) : 0;
        for (auto& [wid, w] : LiveWindows()) w->SetColorScheme(mode);
        resultJson = "null";
        return true;
    }

    // ── menu: menubar de aplicación ──────────────────────────────────────
    if (cmd == "menu.setApplicationMenu") {
        const V* items = params.Find("items");
        const std::string itemsJson = items ? items->Serialize() : "[]";
        for (auto& [wid, w] : LiveWindows()) w->SetApplicationMenu(itemsJson);
        resultJson = "null";
        return true;
    }

    // ── protocol: esquemas personalizados (main → kernel) ────────────────
    if (cmd == "protocol.register") {
        const V* name = params.Find("scheme");
        if (!name || !name->IsString()) { error = "scheme requerido"; return false; }
        ProtocolScheme s;
        s.name = name->AsString();
        if (const V* p = params.Find("privileged"); p && p->IsObject()) {
            auto flag = [p](const char* k, bool d) {
                const V* v = p->Find(k);
                return (v && v->IsBool()) ? v->AsBool() : d;
            };
            s.secure = flag("secure", true);
            s.cors = flag("cors", true);
            s.stream = flag("stream", false);
            s.standard = flag("standard", true);
            s.fetch = flag("fetch", true);
        }
        if (const V* dir = params.Find("dir"); dir && dir->IsString()) {
            s.serveDir = true;
            s.root = dir->AsString();
        }
        if (const V* h = params.Find("handler"); h && h->IsBool()) s.hasHandler = h->AsBool();
        ProtocolRegistry::Get().Register(s);
        // Registro tardío: aplícalo a las ventanas ya abiertas.
        for (auto& [wid, w] : LiveWindows()) w->RegisterProtocol(s.name);
        resultJson = "null";
        return true;
    }
    if (cmd == "protocol.respond") {
        const V* rid = params.Find("reqId");
        if (!rid || !rid->IsNumber()) { error = "reqId requerido"; return false; }
        const V* st = params.Find("status");
        const int status = (st && st->IsNumber()) ? static_cast<int>(st->AsInt()) : 200;
        const V* h = params.Find("headers");
        const std::string headers = h ? h->Serialize() : "{}";
        const V* b = params.Find("body");
        const std::string body = (b && b->IsString()) ? b->AsString() : std::string();
        ProtocolRegistry::Get().ResolveFromMain(static_cast<uint64_t>(rid->AsInt()), status,
                                                headers, body);
        resultJson = "null";
        return true;
    }

    // ── session: permisos del WebView (main ↔ kernel) ────────────────────
    if (cmd == "session.setPermissionHandler") {
        const V* on = params.Find("enabled");
        PermissionBroker::Get().SetEnabled(on ? (on->IsBool() ? on->AsBool() : true) : true);
        resultJson = "null";
        return true;
    }
    if (cmd == "session.respondPermission") {
        const V* rid = params.Find("id");
        if (!rid || !rid->IsNumber()) { error = "id requerido"; return false; }
        const V* allow = params.Find("allow");
        PermissionBroker::Get().Resolve(static_cast<uint64_t>(rid->AsInt()),
                                        allow && allow->IsBool() ? allow->AsBool() : false);
        resultJson = "null";
        return true;
    }

    // ── webContents: ventanas emergentes (window.open) ───────────────────
    if (cmd == "webContents.setWindowOpenHandler") {
        const V* on = params.Find("enabled");
        WindowOpenBroker::Get().SetEnabled(
            on ? (on->IsBool() ? on->AsBool() : true) : true);
        resultJson = "null";
        return true;
    }
    if (cmd == "webContents.respondWindowOpen") {
        const V* rid = params.Find("id");
        if (!rid || !rid->IsNumber()) { error = "id requerido"; return false; }
        const V* act = params.Find("action");
        const bool allow = !(act && act->IsString() && act->AsString() == "deny");
        WindowOpenBroker::Get().Resolve(static_cast<uint64_t>(rid->AsInt()), allow);
        resultJson = "null";
        return true;
    }

    // ── webRequest (intercepción de requests) ────────────────────────────
    if (cmd == "webRequest.register") {
        std::vector<std::string> patterns;
        if (const V* u = params.Find("urls"); u && u->IsArray())
            for (const auto& p : u->AsArray())
                if (p.IsString()) patterns.push_back(p.AsString());
        if (patterns.empty()) patterns.push_back("*");
        WebRequestBroker::Get().Register(patterns);
        resultJson = "null";
        return true;
    }
    if (cmd == "webRequest.unregister") {
        WebRequestBroker::Get().Unregister();
        resultJson = "null";
        return true;
    }
    if (cmd == "webRequest.respond") {
        const V* rid = params.Find("id");
        if (!rid || !rid->IsNumber()) { error = "id requerido"; return false; }
        const V* cancel = params.Find("cancel");
        const V* redirect = params.Find("redirectURL");
        WebRequestBroker::Get().Resolve(
            static_cast<uint64_t>(rid->AsInt()),
            cancel && cancel->IsBool() ? cancel->AsBool() : false,
            (redirect && redirect->IsString()) ? redirect->AsString() : std::string());
        resultJson = "null";
        return true;
    }

    // ── puente Node (renderer ↔ proceso principal) ───────────────────────
    // El renderer llama handlers del main; estos comandos van en sentido
    // inverso (main → kernel → renderer).
    if (cmd == "node.respond") {
        const V* rid = params.Find("reqId");
        if (!rid || !rid->IsNumber()) {
            error = "reqId requerido";
            return false;
        }
        const V* okv = params.Find("ok");
        const bool ok = (!okv || okv->IsBool()) ? (!okv ? true : okv->AsBool()) : true;
        const V* r = params.Find("result");
        std::string resJson = r ? r->Serialize() : "null";
        auto it = pendingNodeCalls_.find(static_cast<uint64_t>(rid->AsInt()));
        if (it != pendingNodeCalls_.end()) {
            Window::Impl::ResolveInvoke(it->second.windowId, it->second.invokeId,
                                        ok, resJson);
            pendingNodeCalls_.erase(it);
        }
        resultJson = "null";
        return true;
    }
    if (cmd == "node.emit") {
        const V* name = params.Find("name");
        const V* payload = params.Find("payload");
        const V* wid = params.Find("windowId");
        const std::string nameS =
            (name && name->IsString()) ? name->AsString() : "node.event";
        const std::string payloadJson = payload ? payload->Serialize() : "null";
        if (wid && wid->IsNumber()) {
            auto it = LiveWindows().find(static_cast<WindowId>(wid->AsInt()));
            if (it != LiveWindows().end()) it->second->EmitToJS(nameS, payloadJson);
        } else {
            for (auto& [id, w] : LiveWindows()) w->EmitToJS(nameS, payloadJson);
        }
        resultJson = "null";
        return true;
    }

    if (cmd == "window.eval") {
        const V* js = params.Find("js");
        if (!js || !js->IsString()) { error = "js requerido"; return false; }
        // Respuesta asíncrona: se envía con el mismo id cuando llegue el callback
        w->EvalJS(js->AsString(), [this, clientId, id](std::string_view result) {
            bool ok = result.find("owError") == std::string::npos;
            SendResponse(clientId, id, ok, result, ok ? "" : "eval falló");
        });
        return true; // respuesta ya enviada (o pendiente) — evita doble send
    }

    if (cmd == "window.capturePage") {
        const V* b = params.Find("base64");
        const bool base64 = b && b->IsBool() && b->AsBool();
        // Respuesta asíncrona: el backend captura y responde al terminar
        // (sin pump anidado, que en Windows reentraba y crasheaba).
        w->CapturePage([this, clientId, id, base64](bool ok, const std::string& png) {
            if (!ok) {
                SendResponse(clientId, id, false, "null", "capture falló");
                return;
            }
            std::string result;
            if (base64) {
                json::Object o;
                o.emplace_back("data", V(ow::b64::Encode(png)));
                o.emplace_back("format", V("png"));
                result = V(std::move(o)).Serialize();
            } else {
                const char* sid = ow_shm_put(
                    reinterpret_cast<const uint8_t*>(png.data()), png.size());
                if (!sid || !*sid) {
                    SendResponse(clientId, id, false, "null", "SHM llena");
                    return;
                }
                json::Object shm;
                shm.emplace_back("id", V(std::string(sid)));
                shm.emplace_back("size", V(static_cast<int64_t>(png.size())));
                json::Object o;
                o.emplace_back("__ow_shm", V(std::move(shm)));
                o.emplace_back("format", V("png"));
                result = V(std::move(o)).Serialize();
            }
            SendResponse(clientId, id, true, result, "");
        });
        return true; // respuesta pendiente — evita doble send
    }

    if (cmd == "window.printToPDF") {
        // Respuesta asíncrona: el backend exporta y responde al terminar.
        w->PrintToPDF([this, clientId, id](bool ok, const std::string& pdf) {
            if (!ok) {
                SendResponse(clientId, id, false, "null", "printToPDF falló");
                return;
            }
            json::Object o;
            o.emplace_back("data", V(ow::b64::Encode(pdf)));
            o.emplace_back("format", V("pdf"));
            SendResponse(clientId, id, true, V(std::move(o)).Serialize(), "");
        });
        return true;
    }

    error = "comando desconocido: " + cmd;
    return false;
}

void ControlServer::HandleLine(uint64_t clientId, std::string_view line) {
    if (line.empty()) return;
    auto parsed = json::Parse(line);
    if (!parsed.value || !parsed.value->IsObject()) {
        log::Warn("control", "línea inválida: " + std::string(line.substr(0, 120)));
        return;
    }
    const V& msg = *parsed.value;
    uint64_t reqId = 0;
    std::string cmd;
    std::string paramsJson = "{}";
    if (const V* v = msg.Find("id"); v && v->IsNumber()) reqId = (uint64_t)v->AsInt();
    if (const V* v = msg.Find("cmd"); v && v->IsString()) cmd = v->AsString();
    if (const V* v = msg.Find("params"); v) paramsJson = v->Serialize();

    if (cmd.empty()) return;

    // traza de diagnóstico: qué comando entra y desde qué hilo muere el flujo
    log::Info("control", "cmd: " + cmd);

    // Eventos JS→nativo vía SDK (sin id)
    if (cmd == "event.emit") {
        // reenvía al renderer destino si trae windowId
        BroadcastEvent("sdk.event", paramsJson);
        return;
    }

    std::string resultJson;
    std::string error;
    bool ok = HandleCommand(clientId, reqId, cmd, paramsJson, resultJson, error);

    // window.eval responde async (evita duplicar)
    if (ok && (cmd == "window.eval" || cmd == "window.capturePage" ||
               cmd == "window.printToPDF"))
        return;

    SendResponse(clientId, reqId, ok, resultJson, error);
}

} // namespace ow
