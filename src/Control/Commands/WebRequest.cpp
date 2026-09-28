// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/WebRequest.cpp — comandos `webRequest.*`.
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

bool ControlServer::CmdWebRequest(const std::string& cmd, const json::Value& params,
                        uint64_t clientId, uint64_t id,
                        std::string& resultJson, std::string& error) {
    (void)cmd; (void)clientId; (void)id;
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
    return false;
}

} // namespace ow
