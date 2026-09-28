// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/Menu.cpp — comandos `menu.*`.
#include "../ControlServer.hpp"
#include "Util.hpp"
#include "../../Bridge/Dispatcher.hpp"
#include "../../Protocol/ProtocolRegistry.hpp"
#include "../../Session/PermissionBroker.hpp"
#include "../../Session/WebRequestBroker.hpp"
#include "../../Session/WindowOpenBroker.hpp"
#include "ow/Module.h"
#include "ow/Window.h"
#include "ow/Common.h"

namespace ow {

using V = json::Value;

bool ControlServer::CmdMenu(const std::string& cmd, const json::Value& params,
                        uint64_t clientId, uint64_t id,
                        std::string& resultJson, std::string& error) {
    (void)cmd; (void)clientId; (void)id;
    if (cmd == "menu.setApplicationMenu") {
        const V* items = params.Find("items");
        const std::string itemsJson = items ? items->Serialize() : "[]";
        for (auto& [wid, w] : LiveWindows()) w->SetApplicationMenu(itemsJson);
        resultJson = "null";
        return true;
    }

    // ── protocol: esquemas personalizados (main → kernel) ────────────────
    return false;
}

} // namespace ow
