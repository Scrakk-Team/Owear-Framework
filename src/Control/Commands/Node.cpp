// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/Node.cpp — comandos `node.*`.
#include "../ControlServer.hpp"
#include "../../Runtime/NodeManager.hpp"

namespace ow {

using V = json::Value;

bool ControlServer::CmdNode(const std::string& cmd, const json::Value& params,
                            std::string& resultJson, std::string& error) {
    if (cmd == "node.ensure") {
        std::string range = "latest";
        if (const V* r = params.Find("range"); r && r->IsString()) range = r->AsString();
        auto node = NodeManager::Resolve(range);
        if (node.IsErr()) { error = node.Error(); return false; }
        json::Object o;
        o.emplace_back("path", V(node.Value().bin.string()));
        o.emplace_back("version", V(node.Value().version));
        o.emplace_back("source", V(node.Value().source));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }
    return false;
}

} // namespace ow
