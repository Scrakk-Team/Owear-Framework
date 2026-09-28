// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/Protocol.cpp — comandos `protocol.*`.
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

bool ControlServer::CmdProtocol(const std::string& cmd, const json::Value& params,
                        uint64_t clientId, uint64_t id,
                        std::string& resultJson, std::string& error) {
    (void)cmd; (void)clientId; (void)id;
    if (cmd == "protocol.register") {
        const V* name = params.Find("scheme");
        if (!name || !name->IsString()) { error = "scheme requerido"; return false; }
        ProtocolScheme s;
        s.name = name->AsString();
        if (const V* p = params.Find("privileged"); p && p->IsObject()) {
            auto flag = [p](const char* k, bool d) {
                const V* v = p->Find(k);
                return (v && v->IsBool()) ? v->AsBool() : d;
            };
            s.secure = flag("secure", true);
            s.cors = flag("cors", true);
            s.stream = flag("stream", false);
            s.standard = flag("standard", true);
            s.fetch = flag("fetch", true);
        }
        if (const V* dir = params.Find("dir"); dir && dir->IsString()) {
            s.serveDir = true;
            s.root = dir->AsString();
        }
        if (const V* h = params.Find("handler"); h && h->IsBool()) s.hasHandler = h->AsBool();
        ProtocolRegistry::Get().Register(s);
        // Registro tardío: aplícalo a las ventanas ya abiertas.
        for (auto& [wid, w] : LiveWindows()) w->RegisterProtocol(s.name);
        resultJson = "null";
        return true;
    }
    if (cmd == "protocol.respond") {
        const V* rid = params.Find("reqId");
        if (!rid || !rid->IsNumber()) { error = "reqId requerido"; return false; }
        const V* st = params.Find("status");
        const int status = (st && st->IsNumber()) ? static_cast<int>(st->AsInt()) : 200;
        const V* h = params.Find("headers");
        const std::string headers = h ? h->Serialize() : "{}";
        const V* b = params.Find("body");
        const std::string body = (b && b->IsString()) ? b->AsString() : std::string();
        ProtocolRegistry::Get().ResolveFromMain(static_cast<uint64_t>(rid->AsInt()), status,
                                                headers, body);
        resultJson = "null";
        return true;
    }

    // ── session: permisos del WebView (main ↔ kernel) ────────────────────
    if (cmd == "session.setPermissionHandler") {
        const V* on = params.Find("enabled");
        PermissionBroker::Get().SetEnabled(on ? (on->IsBool() ? on->AsBool() : true) : true);
        resultJson = "null";
        return true;
    }
    return false;
}

} // namespace ow
