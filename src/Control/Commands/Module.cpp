// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/Module.cpp — comandos `module.*` (módulos nativos desde
// el proceso principal: la pieza que hace la API "tipo Electron").
#include "../ControlServer.hpp"
#include "Util.hpp"
#include "../../Bridge/Dispatcher.hpp"
#include "ow/Module.h"
#include "ow/Window.h"

namespace ow {

using V = json::Value;

bool ControlServer::CmdModule(const std::string& cmd, const json::Value& params,
                              std::string& resultJson, std::string& error) {
    if (cmd == "module.invoke") {
        const V* mod = params.Find("module");
        const V* fn = params.Find("method");
        if (!mod || !mod->IsString()) { error = "module requerido"; return false; }
        if (!fn || !fn->IsString()) { error = "method requerido"; return false; }

        std::string argsJson = "[]";
        if (const V* a = params.Find("args"); a) argsJson = a->Serialize();

        uint32_t winId = 0;
        if (const V* wid = params.Find("windowId"); wid && wid->IsNumber())
            winId = static_cast<uint32_t>(wid->AsInt());

        ow_request_t req{};
        req.json = argsJson.c_str();
        req.json_len = static_cast<uint32_t>(argsJson.size());
        req.window_id = winId;

        ow_response_t res{};
        Dispatcher::Get().Execute(winId, mod->AsString(), fn->AsString(), &req, &res);
        if (res.status != 0) {
            error = res.error ? std::string(res.error) : std::string("error en el módulo");
            return false;
        }
        resultJson.assign(res.json ? res.json : "null", res.json_len);
        return true;
    }
    if (cmd == "module.list") {
        json::Array arr;
        for (const auto& m : Dispatcher::Get().Modules())
            arr.emplace_back(V(CtModuleInfoJson(m)));
        resultJson = V(std::move(arr)).Serialize();
        return true;
    }
    if (cmd == "module.info") {
        std::string name;
        if (const V* n = params.Find("name"); n && n->IsString()) name = n->AsString();
        Dispatcher::ModuleInfo m;
        if (name.empty() || !Dispatcher::Get().Module(name, m)) {
            error = "módulo desconocido: " + name;
            return false;
        }
        resultJson = V(CtModuleInfoJson(m)).Serialize();
        return true;
    }
    return false;
}

} // namespace ow
