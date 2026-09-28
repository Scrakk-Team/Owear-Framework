// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/WebContents.cpp — comandos `webContents.*`.
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

bool ControlServer::CmdWebContents(const std::string& cmd, const json::Value& params,
                        uint64_t clientId, uint64_t id,
                        std::string& resultJson, std::string& error) {
    (void)cmd; (void)clientId; (void)id;
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
    return false;
}

} // namespace ow
