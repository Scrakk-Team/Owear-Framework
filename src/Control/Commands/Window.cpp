// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/Window.cpp — comandos `window.*` (crear/estado/geometría/eval/capture/print).
#include "../ControlServer.hpp"
#include "Util.hpp"
#include "../../Bridge/Dispatcher.hpp"
#include "../../Protocol/ProtocolRegistry.hpp"
#include "../../Session/PermissionBroker.hpp"
#include "../../Session/WebRequestBroker.hpp"
#include "../../Session/WindowOpenBroker.hpp"
#include "ow/Module.h"
#include "ow/Common.h"
#include "../../Window/Window_p.hpp"
#include "ow/Window.h"
#include "ow/Base64.h"
#include "ow/Shm.h"

namespace ow {

using V = json::Value;

bool ControlServer::CmdWindow(const std::string& cmd, const json::Value& params,
                             uint64_t clientId, uint64_t id,
                             std::string& resultJson, std::string& error) {
    (void)cmd;
    auto getWindow = [&](Window** out) -> bool {
        const V* wid = params.IsArray() ? (params.AsArray().empty() ? nullptr : &params.AsArray()[0])
                                        : params.Find("windowId");
        if (!wid || !wid->IsNumber()) { error = "windowId requerido"; return false; }
        auto it = LiveWindows().find(static_cast<WindowId>(wid->AsInt()));
        if (it == LiveWindows().end()) { error = "ventana no encontrada"; return false; }
        *out = it->second;
        return true;
    };

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
        // C9: opciones de ventana
        if (const V* v = params.Find("show"); v && v->IsBool()) opts.show = v->AsBool();
        if (const V* v = params.Find("parent"); v && v->IsNumber())
            opts.parent = static_cast<WindowId>(v->AsInt());
        if (const V* v = params.Find("modal"); v && v->IsBool()) opts.modal = v->AsBool();
        if (const V* v = params.Find("transparent"); v && v->IsBool())
            opts.transparent = v->AsBool();
        if (const V* v = params.Find("backgroundColor"); v && v->IsString())
            opts.backgroundColor = v->AsString();
        if (const V* v = params.Find("movable"); v && v->IsBool()) opts.movable = v->AsBool();
        if (const V* v = params.Find("minimizable"); v && v->IsBool())
            opts.minimizable = v->AsBool();
        if (const V* v = params.Find("maximizable"); v && v->IsBool())
            opts.maximizable = v->AsBool();
        if (const V* v = params.Find("closable"); v && v->IsBool()) opts.closable = v->AsBool();
        if (const V* v = params.Find("fullscreenable"); v && v->IsBool())
            opts.fullscreenable = v->AsBool();
        if (const V* v = params.Find("skipTaskbar"); v && v->IsBool())
            opts.skipTaskbar = v->AsBool();
        if (const V* v = params.Find("alwaysOnTop"); v && v->IsBool())
            opts.alwaysOnTop = v->AsBool();
        if (const V* v = params.Find("hasShadow"); v && v->IsBool())
            opts.hasShadow = v->AsBool();
        if (const V* v = params.Find("minWidth"); v && v->IsNumber())
            opts.minWidth = static_cast<int>(v->AsInt());
        if (const V* v = params.Find("minHeight"); v && v->IsNumber())
            opts.minHeight = static_cast<int>(v->AsInt());
        if (const V* v = params.Find("maxWidth"); v && v->IsNumber())
            opts.maxWidth = static_cast<int>(v->AsInt());
        if (const V* v = params.Find("maxHeight"); v && v->IsNumber())
            opts.maxHeight = static_cast<int>(v->AsInt());
        if (const V* v = params.Find("aspectRatio"); v && v->IsNumber())
            opts.aspectRatio = v->AsDouble();

        auto* win = new Window(opts); // se auto-registra en LiveWindows
        WindowId wid = win->Id();

        json::Object o;
        o.emplace_back("windowId", V(static_cast<int64_t>(wid)));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }

    // ── C9: estáticos de ventana ─────────────────────────────────────────
    if (cmd == "window.setColorScheme") {
        const V* s = params.Find("scheme");
        const int mode = (s && s->IsNumber()) ? static_cast<int>(s->AsInt()) : 0;
        for (auto& [wid, w] : LiveWindows()) w->SetColorScheme(mode);
        resultJson = "null";
        return true;
    }
    if (cmd == "window.list") {
        json::Array arr;
        for (auto& [wid, wp] : LiveWindows()) arr.emplace_back(V(static_cast<int64_t>(wid)));
        resultJson = V(std::move(arr)).Serialize();
        return true;
    }
    if (cmd == "window.getFocused") {
        resultJson = V(static_cast<int64_t>(g_focusedWindow)).Serialize();
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

    // ── C9: estado/estilo de ventana ─────────────────────────────────────
    auto boolResult = [&](bool b) { resultJson = b ? "true" : "false"; };
    auto sizeResult = [&](int width, int height) {
        json::Object o;
        o.emplace_back("width", V(static_cast<int64_t>(width)));
        o.emplace_back("height", V(static_cast<int64_t>(height)));
        resultJson = V(std::move(o)).Serialize();
    };
    auto rectResult = [&](const Window::Bounds& b) {
        json::Object o;
        o.emplace_back("x", V(static_cast<int64_t>(b.x)));
        o.emplace_back("y", V(static_cast<int64_t>(b.y)));
        o.emplace_back("width", V(static_cast<int64_t>(b.w)));
        o.emplace_back("height", V(static_cast<int64_t>(b.h)));
        resultJson = V(std::move(o)).Serialize();
    };
    auto boolArg = [&](const char* k, bool dflt) {
        const V* v = params.Find(k);
        return (v && v->IsBool()) ? v->AsBool() : dflt;
    };
    auto numArg = [&](const char* k, int dflt) {
        const V* v = params.Find(k);
        return (v && v->IsNumber()) ? static_cast<int>(v->AsInt()) : dflt;
    };

    if (cmd == "window.isVisible") { boolResult(w->IsVisible()); return true; }
    if (cmd == "window.isFocused") { boolResult(w->IsFocused()); return true; }
    if (cmd == "window.isResizable") { boolResult(w->IsResizable()); return true; }
    if (cmd == "window.isMovable") { boolResult(w->IsMovable()); return true; }
    if (cmd == "window.isMinimizable") { boolResult(w->IsMinimizable()); return true; }
    if (cmd == "window.isMaximizable") { boolResult(w->IsMaximizable()); return true; }
    if (cmd == "window.isClosable") { boolResult(w->IsClosable()); return true; }
    if (cmd == "window.isAlwaysOnTop") { boolResult(w->IsAlwaysOnTop()); return true; }
    if (cmd == "window.isKiosk") { boolResult(w->IsKiosk()); return true; }
    if (cmd == "window.isDestroyed") { boolResult(w->IsDestroyed()); return true; }
    if (cmd == "window.isFullScreen") { boolResult(w->IsFullScreen()); return true; }
    if (cmd == "window.setResizable") { w->SetResizable(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setMovable") { w->SetMovable(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setMinimizable") { w->SetMinimizable(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setMaximizable") { w->SetMaximizable(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setClosable") { w->SetClosable(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setAlwaysOnTop") {
        w->SetAlwaysOnTop(boolArg("on", true), numArg("level", 0));
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setSkipTaskbar") { w->SetSkipTaskbar(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setHasShadow") { w->SetHasShadow(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setKiosk") { w->SetKiosk(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setIgnoreMouseEvents") {
        w->SetIgnoreMouseEvents(boolArg("ignore", true), boolArg("forward", false));
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setProgressBar") {
        const V* v = params.Find("value");
        std::string mode = "normal";
        if (const V* m = params.Find("mode"); m && m->IsString()) mode = m->AsString();
        w->SetProgressBar(v && v->IsNumber() ? v->AsDouble() : 0.0, mode);
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setBackgroundColor") {
        const V* c = params.Find("color");
        w->SetBackgroundColor(c && c->IsString() ? c->AsString() : std::string());
        resultJson = "null";
        return true;
    }
    if (cmd == "window.moveTop") { w->MoveTop(); resultJson = "null"; return true; }
    if (cmd == "window.setAspectRatio") {
        const V* r = params.Find("ratio");
        w->SetAspectRatio(r && r->IsNumber() ? r->AsDouble() : 0.0, numArg("extraW", 0),
                          numArg("extraH", 0));
        resultJson = "null";
        return true;
    }
    if (cmd == "window.getContentBounds") { rectResult(w->GetContentBounds()); return true; }
    if (cmd == "window.setContentSize") {
        w->SetContentSize(numArg("width", 0), numArg("height", 0));
        resultJson = "null";
        return true;
    }
    if (cmd == "window.getContentSize") {
        auto s = w->GetContentSize();
        sizeResult(s.width, s.height);
        return true;
    }
    if (cmd == "window.getMinimumSize") {
        auto s = w->GetMinimumSize();
        sizeResult(s.width, s.height);
        return true;
    }
    if (cmd == "window.getMaximumSize") {
        auto s = w->GetMaximumSize();
        sizeResult(s.width, s.height);
        return true;
    }
    if (cmd == "window.setMinimumSize") {
        w->SetMinimumSize(numArg("width", 0), numArg("height", 0));
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setMaximumSize") {
        w->SetMaximumSize(numArg("width", 0), numArg("height", 0));
        resultJson = "null";
        return true;
    }
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

    // ── menu: menubar de aplicación ──────────────────────────────────────
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

    return false;
}

} // namespace ow
