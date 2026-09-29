// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/ControlServer.cpp — lógica común de comandos.
// Transporte por plataforma en ControlServer_<platform>.cpp.
//
#include "../ControlServer.hpp"
#include "../Commands/Util.hpp"

#include "../../Bridge/Dispatcher.hpp"
#include "../../Core/App.hpp"
#include "../../Protocol/ProtocolRegistry.hpp"
#include "../../Session/PermissionBroker.hpp"
#include "../../Session/WebRequestBroker.hpp"
#include "../../Session/WindowOpenBroker.hpp"
#include "../../Window/Window_p.hpp"
#include "../../Core/Log.hpp"
#include "../../Runtime/NodeManager.hpp"
#include "ow/App.h"
#include "ow/Window.h"
#include "ow/Base64.h"
#include "ow/Shm.h"
#include "ow/detail/minjson.hpp"

#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif


namespace ow {

using V = json::Value;

namespace {
json::Object ModuleInfoJson(const Dispatcher::ModuleInfo& m) {
    json::Object o;
    o.emplace_back("name", V(m.name));
    o.emplace_back("version", V(m.version));
    o.emplace_back("origin", V(m.origin));
    o.emplace_back("builtin", V(m.builtin));
    o.emplace_back("functions", V(static_cast<int64_t>(m.functions.size())));
    json::Array names;
    for (const auto& fn : m.functions) names.emplace_back(V(fn));
    o.emplace_back("functionNames", V(std::move(names)));
    return o;
}
} // namespace

bool ControlServer::HandleCommand(uint64_t clientId, uint64_t id,
                                  const std::string& cmd,
                                  std::string_view paramsJson,
                                  std::string& resultJson, std::string& error) {
    auto parsed = json::Parse(paramsJson);
    V params = parsed.value ? std::move(*parsed.value) : V(json::Object{});

    if (cmd.rfind("app.", 0) == 0) return CmdApp(cmd, params, resultJson, error);
    if (cmd == "node.ensure") return CmdNode(cmd, params, resultJson, error);
    if (cmd.rfind("module.", 0) == 0) return CmdModule(cmd, params, resultJson, error);
    if (cmd.rfind("window.", 0) == 0)
        return CmdWindow(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("menu.", 0) == 0)
        return CmdMenu(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("protocol.", 0) == 0)
        return CmdProtocol(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("session.", 0) == 0)
        return CmdSession(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("webContents.", 0) == 0)
        return CmdWebContents(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("webRequest.", 0) == 0)
        return CmdWebRequest(cmd, params, clientId, id, resultJson, error);
    if (cmd.rfind("node.", 0) == 0)
        return CmdNodeBridge(cmd, params, clientId, id, resultJson, error);

    error = "comando desconocido: " + cmd;
    return false;
}

void ControlServer::HandleLine(uint64_t clientId, std::string_view line) {
    if (line.empty()) return;
    auto parsed = json::Parse(line);
    if (!parsed.value || !parsed.value->IsObject()) {
        log::Warn("control", "línea inválida: " + std::string(line.substr(0, 120)));
        return;
    }
    const V& msg = *parsed.value;
    uint64_t reqId = 0;
    std::string cmd;
    std::string paramsJson = "{}";
    if (const V* v = msg.Find("id"); v && v->IsNumber()) reqId = (uint64_t)v->AsInt();
    if (const V* v = msg.Find("cmd"); v && v->IsString()) cmd = v->AsString();
    if (const V* v = msg.Find("params"); v) paramsJson = v->Serialize();

    if (cmd.empty()) return;

    // traza de diagnóstico: qué comando entra y desde qué hilo muere el flujo
    log::Info("control", "cmd: " + cmd);

    // Eventos JS→nativo vía SDK (sin id)
    if (cmd == "event.emit") {
        // reenvía al renderer destino si trae windowId
        BroadcastEvent("sdk.event", paramsJson);
        return;
    }

    std::string resultJson;
    std::string error;
    bool ok = HandleCommand(clientId, reqId, cmd, paramsJson, resultJson, error);

    // window.eval responde async (evita duplicar)
    if (ok && (cmd == "window.eval" || cmd == "window.capturePage" ||
               cmd == "window.printToPDF"))
        return;

    SendResponse(clientId, reqId, ok, resultJson, error);
}


} // namespace ow
