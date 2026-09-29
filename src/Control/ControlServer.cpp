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

} // namespace ow
