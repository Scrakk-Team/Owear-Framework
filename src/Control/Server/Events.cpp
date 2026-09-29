// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/ControlServer.cpp — lógica común de comandos.
// Transporte por plataforma en ControlServer_<platform>.cpp.
//
#include "../ControlServer.hpp"
#include "../Commands/Util.hpp"

#include "../../Bridge/Dispatcher.hpp"
#include "../../Core/App.hpp"
#include "../../Protocol/ProtocolRegistry.hpp"
#include "../../Session/PermissionBroker.hpp"
#include "../../Session/WebRequestBroker.hpp"
#include "../../Session/WindowOpenBroker.hpp"
#include "../../Window/Window_p.hpp"
#include "../../Core/Log.hpp"
#include "../../Runtime/NodeManager.hpp"
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


} // namespace ow
