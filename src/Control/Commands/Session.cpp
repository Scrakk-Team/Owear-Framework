// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/Session.cpp — comandos `session.*`.
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

bool ControlServer::CmdSession(const std::string& cmd, const json::Value& params,
                        uint64_t clientId, uint64_t id,
                        std::string& resultJson, std::string& error) {
    (void)cmd; (void)clientId; (void)id;
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
    return false;
}

} // namespace ow
