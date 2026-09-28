// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/ControlServer.cpp — lógica común de comandos.
// Transporte por plataforma en ControlServer_<platform>.cpp.
//
#include "ControlServer.hpp"
#include "Commands/Util.hpp"

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

/// Ventana con foco (para `window.getFocused`). La actualizan los eventos
/// focus/blur de cada ventana (WireWindowEvents).
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

void ControlServer::RegisterWindow(Window* w) {
    if (!w) return;
    const WindowId id = w->Id();
    LiveWindows()[id] = w;
    Get().WireWindowEvents(id, w);
}

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
    w->On("focus", [this, id, fwd = forward("focus")](std::string_view p) {
        g_focusedWindow = id;
        fwd(p);
    });
    w->On("blur", [this, id, fwd = forward("blur")](std::string_view p) {
        if (g_focusedWindow == id) g_focusedWindow = 0;
        fwd(p);
    });
    w->On("maximize", forward("maximize"));
    w->On("unmaximize", forward("unmaximize"));
    w->On("enterFullScreen", forward("enterFullScreen"));
    w->On("leaveFullScreen", forward("leaveFullScreen"));
    // C9: eventos extra
    w->On("show", forward("show"));
    w->On("hide", forward("hide"));
    w->On("restore", forward("restore"));
    w->On("minimize", forward("minimize"));
    w->On("resized", forward("resized"));
    w->On("moved", forward("moved"));
    w->On("alwaysOnTopChanged", forward("alwaysOnTopChanged"));
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

    if (cmd.rfind("app.", 0) == 0) return CmdApp(cmd, params, resultJson, error);
    if (cmd == "node.ensure") return CmdNode(cmd, params, resultJson, error);
    if (cmd.rfind("module.", 0) == 0) return CmdModule(cmd, params, resultJson, error);
    if (cmd.rfind("window.", 0) == 0)
        return CmdWindow(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("menu.", 0) == 0)
        return CmdMenu(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("protocol.", 0) == 0)
        return CmdProtocol(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("session.", 0) == 0)
        return CmdSession(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("webContents.", 0) == 0)
        return CmdWebContents(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("webRequest.", 0) == 0)
        return CmdWebRequest(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("node.", 0) == 0)
        return CmdNodeBridge(cmd, params, clientId, id, resultJson, error);

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
