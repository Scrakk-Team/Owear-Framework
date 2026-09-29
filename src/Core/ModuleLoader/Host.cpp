// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/ModuleLoader/Host.cpp — host ABI (emit_event/log) para modulos nativos.
#include "Internal.hpp"
#include "../Log.hpp"
#include "../../Control/ControlServer.hpp"
#include "ow/App.h"
#include "ow/Window.h"
#include "ow/detail/minjson.hpp"

#include <string>
#include <utility>

namespace ow {
namespace {

void HostEmitEvent(void*, uint32_t window_id, const char* name, const char* json) {
    // Los módulos emiten desde hilos de fondo (watchers, PTY, pipes):
    // TODO el trabajo con GTK/sockets se marshaling al main thread.
    std::string payload = json && json[0] ? json : "null";
    std::string evtName = name ? name : "";
    App::Post([window_id, evtName = std::move(evtName), payload = std::move(payload)] {
        using json::Value;

        if (window_id != 0) {
            auto it = LiveWindows().find(window_id);
            if (it != LiveWindows().end()) it->second->EmitToJS(evtName, payload);
            return;
        }

        json::Object params;
        params.emplace_back("name", Value(evtName));
        {
            auto parsed = json::Parse(payload);
            params.emplace_back("payload",
                                parsed.value ? std::move(*parsed.value) : Value(nullptr));
        }
        ControlServer::Get().BroadcastEvent("module.event",
                                            Value(std::move(params)).Serialize());
        for (auto& [id, w] : LiveWindows()) w->EmitToJS(evtName, payload);
    });
}

void HostLog(void*, int level, const char* msg) {
    using L = log::Level;
    L l = level <= 0 ? L::Debug : level == 1 ? L::Info : level == 2 ? L::Warn : L::Error;
    log::Write(l, "module", msg ? msg : "");
}

ow_module_host_t MakeHost() {
    ow_module_host_t h{};
    h.version = OW_HOST_ABI_VERSION;
    h.ctx = nullptr;
    h.emit_event = &HostEmitEvent;
    h.log = &HostLog;
    return h;
}
const ow_module_host_t g_host = MakeHost();

} // namespace

const ow_module_host_t& ModuleHost() { return g_host; }

} // namespace ow
