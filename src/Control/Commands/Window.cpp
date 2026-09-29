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

    if (cmd == "window.create") return CmdWindowCreate(params, resultJson);

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

    if (CmdWindowState(cmd, params, w, resultJson, error)) return true;
    if (CmdWindowPage(cmd, params, clientId, id, w, resultJson, error)) return true;

    return false;
}

} // namespace ow
