// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/NodeBridge.cpp — comandos `node.respond`/`node.emit` (puente asíncrono).
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

namespace ow {

using V = json::Value;

bool ControlServer::CmdNodeBridge(const std::string& cmd, const json::Value& params,
                                 uint64_t clientId, uint64_t id,
                                 std::string& resultJson, std::string& error) {
    (void)cmd; (void)clientId; (void)id;
    if (cmd == "node.respond") {
        const V* rid = params.Find("reqId");
        if (!rid || !rid->IsNumber()) {
            error = "reqId requerido";
            return false;
        }
        const V* okv = params.Find("ok");
        const bool ok = (!okv || okv->IsBool()) ? (!okv ? true : okv->AsBool()) : true;
        const V* r = params.Find("result");
        std::string resJson = r ? r->Serialize() : "null";
        auto it = pendingNodeCalls_.find(static_cast<uint64_t>(rid->AsInt()));
        if (it != pendingNodeCalls_.end()) {
            Window::Impl::ResolveInvoke(it->second.windowId, it->second.invokeId,
                                        ok, resJson);
            pendingNodeCalls_.erase(it);
        }
        resultJson = "null";
        return true;
    }
    if (cmd == "node.emit") {
        const V* name = params.Find("name");
        const V* payload = params.Find("payload");
        const V* wid = params.Find("windowId");
        const std::string nameS =
            (name && name->IsString()) ? name->AsString() : "node.event";
        const std::string payloadJson = payload ? payload->Serialize() : "null";
        if (wid && wid->IsNumber()) {
            auto it = LiveWindows().find(static_cast<WindowId>(wid->AsInt()));
            if (it != LiveWindows().end()) it->second->EmitToJS(nameS, payloadJson);
        } else {
            for (auto& [id, w] : LiveWindows()) w->EmitToJS(nameS, payloadJson);
        }
        resultJson = "null";
        return true;
    }

    return false;
}

} // namespace ow
