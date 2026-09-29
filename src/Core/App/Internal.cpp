// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/App/Internal.cpp — bootstrap y utilidades del namespace internal.
// El main loop vive en App_<platform>.cpp.
//
#include "../App.hpp"
#include "../ModuleLoader.hpp"

#include "../../Bridge/Dispatcher.hpp"
#include "../../Control/ControlServer.hpp"
#include "../../Pack/Pack.hpp"
#include "../../Runtime/NodeManager.hpp"
#include "../Log.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <climits>
#endif

namespace ow {

namespace {
AppOptions g_options;
bool g_initialized = false;
int g_exitCode = 0;
std::string g_appName;
std::vector<std::string> g_cmdArgs;
} // namespace

namespace internal {

// El registro de builtins se GENERA desde los manifiestos de API
// (api/<nombre>/owear.module.json, kind=builtin). Ver tools/gen-apis.mjs y
// src/Core/BuiltinRegistry.generated.cpp. No hardcodear módulos aquí.
void RegisterGeneratedBuiltins();

namespace {
void SetEnv(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}
} // namespace

bool Bootstrap(int argc, char** argv, const AppOptions& options) {
    if (g_initialized) return true;
    g_options = options;

    if (!PlatformInit(argc, argv)) return false;

    // Single binary: si el payload va embebido al final del propio ejecutable,
    // se extrae una vez y se expone como si fueran ficheros externos (módulos,
    // assets y main). Así el resto del kernel no cambia.
    if (pack::HasPayload()) {
        const std::string dir = pack::EnsureExtracted();
        if (!dir.empty()) {
            std::string mods = dir + "/modules";
            if (const char* cur = std::getenv("OW_MODULES_DIR"); cur && *cur)
                mods += std::string(":") + cur;
            SetEnv("OW_MODULES_DIR", mods);
            if (!std::getenv("OW_ASSETS_DIR") &&
                std::filesystem::is_directory(dir + "/app"))
                SetEnv("OW_ASSETS_DIR", dir + "/app");
            if (!std::getenv("OW_APP_MAIN") &&
                std::filesystem::exists(dir + "/app/main.js"))
                SetEnv("OW_APP_MAIN", dir + "/app/main.js");
            log::Info("app", "single-binary: payload en " + dir);
        }
    }

    RegisterGeneratedBuiltins();
    ModuleLoader::ProvideHostToBuiltins();

    size_t loaded = ModuleLoader::LoadAll();
    log::Info("app", "funciones de módulos dinámicos cargadas: " + std::to_string(loaded));
    log::StartupMark("modulos cargados");

    // Servidor de control: siempre activo (el SDK JS conecta por aquí).
    if (!ControlServer::Get().Start()) {
        log::Warn("app", "control server no disponible; modo nativo-only");
    } else {
        log::Info("app", "control socket: " + ControlServer::Get().SocketPath());
        log::StartupMark("control socket");
    }

    // Modo JS-driven: spawn del sidecar con el entry de la app.
    const char* appMainRaw = std::getenv("OW_APP_MAIN");
    if (appMainRaw && *appMainRaw) {
        const std::string appMain = appMainRaw;
        if (appMain.size() > 3 && appMain.compare(appMain.size() - 3, 3, ".ts") == 0) {
            log::Warn("app", "OW_APP_MAIN apunta a TypeScript (" + appMain +
                                 "): node no ejecuta .ts sin --experimental-strip-types. "
                                 "Usa `ow dev`/`ow build`, que compilan el main a JavaScript");
        }
        auto node = NodeManager::Resolve("latest");
        if (node.IsOk()) {
            const auto& rt = node.Value();
            log::Info("app", "runtime node " + rt.version + " (" + rt.source + "): " +
                                 rt.bin.string());
            long pid = NodeManager::Spawn(rt.bin, appMain);
            if (pid > 0)
                log::Info("app", "sidecar node pid=" + std::to_string(pid) + " → " + appMain);
            else
                log::Error("app", "no se pudo lanzar el sidecar node");
        } else {
            log::Error("app", "node runtime no disponible: " + node.Error());
        }
    }

    g_initialized = true;
    return true;
}

void RequestQuit(int exitCode) {
    g_exitCode = exitCode;
    PlatformQuit();
}

int ExitCode() { return g_exitCode; }

std::string ExecutableDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string exe(buf);
    const auto s = exe.find_last_of("\\/");
    return s == std::string::npos ? std::string() : exe.substr(0, s);
#else
    char buf[PATH_MAX] = {};
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    std::string exe = n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
    const auto s = exe.find_last_of('/');
    return s == std::string::npos ? std::string() : exe.substr(0, s);
#endif
}

std::string AppName() {
    if (!g_appName.empty()) return g_appName;
    if (const char* n = std::getenv("OW_APP_NAME"); n && *n) return n;
    return "Owear App";
}

void SetAppName(const std::string& name) { g_appName = name; }

void AppendCommandArg(const std::string& arg) {
    if (!arg.empty()) g_cmdArgs.push_back(arg);
}

const std::vector<std::string>& CommandArgs() { return g_cmdArgs; }


const AppOptions& OptionsRef() { return g_options; }

} // namespace internal

} // namespace ow
