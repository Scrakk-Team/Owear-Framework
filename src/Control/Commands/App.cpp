// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/App.cpp — comandos `app.*`.
#include "../ControlServer.hpp"
#include "Util.hpp"
#include "../../Core/App.hpp"

#include <cstdlib>
#include <filesystem>

namespace ow {

using V = json::Value;

bool ControlServer::CmdApp(const std::string& cmd, const json::Value& params,
                           std::string& resultJson, std::string& error) {
    if (cmd == "app.info") {
        json::Object o;
        o.emplace_back("pid", V(CtCurrentPid()));
        o.emplace_back("version", V(OW_VERSION_STRING));
        o.emplace_back("socket", V(socketPath_));
        o.emplace_back("name", V(internal::AppName()));

        const char* mainEnv = std::getenv("OW_APP_MAIN");
        const char* assetsEnv = std::getenv("OW_ASSETS_DIR");
        std::string appPath;
        if (mainEnv && *mainEnv)
            appPath = std::filesystem::path(mainEnv).parent_path().string();
        else if (assetsEnv && *assetsEnv)
            appPath = assetsEnv;
        else {
            std::error_code ec;
            appPath = std::filesystem::current_path(ec).string();
        }
        o.emplace_back("appPath", V(appPath));
        o.emplace_back("exePath", V(internal::ExecutableDir()));
        const char* packagedEnv = std::getenv("OW_PACKAGED");
        const bool packaged =
            (packagedEnv && std::string(packagedEnv) == "1") ||
            ((!mainEnv || !*mainEnv) && assetsEnv && *assetsEnv);
        o.emplace_back("packaged", V(packaged));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }
    if (cmd == "app.setName") {
        const V* n = params.Find("name");
        if (!n || !n->IsString()) { error = "name requerido"; return false; }
        internal::SetAppName(n->AsString());
        resultJson = "null";
        return true;
    }
    if (cmd == "app.commandLine.appendSwitch") {
        const V* k = params.Find("key");
        if (!k || !k->IsString()) { error = "key requerido"; return false; }
        std::string arg = "--" + k->AsString();
        if (const V* v = params.Find("value"); v && v->IsString() && !v->AsString().empty())
            arg += "=" + v->AsString();
        internal::AppendCommandArg(arg);
        resultJson = "null";
        return true;
    }
    if (cmd == "app.commandLine.appendArgument") {
        const V* a = params.Find("arg");
        if (!a || !a->IsString()) { error = "arg requerido"; return false; }
        internal::AppendCommandArg(a->AsString());
        resultJson = "null";
        return true;
    }
    if (cmd == "app.quit") {
        // Responde PRIMERO y pide el quit DESPUÉS (con margen): si Quit
        // postea WM_QUIT inmediatamente, el main loop sale y Stop cierra
        // la pipe antes de que la respuesta llegue al cliente.
        resultJson = "null";
        internal::PlatformDelay(300, [] { App::Quit(0); });
        return true;
    }
    return false;
}

} // namespace ow
