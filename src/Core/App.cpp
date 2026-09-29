// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/App.cpp — estado común de la aplicación.
// El main loop vive en App_<platform>.cpp.
//
#include "App.hpp"
#include "ModuleLoader.hpp"

#include "../Bridge/Dispatcher.hpp"
#include "../Control/ControlServer.hpp"
#include "../Pack/Pack.hpp"
#include "../Runtime/NodeManager.hpp"
#include "Log.hpp"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
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
std::function<void()> g_onReady;
} // namespace

const AppOptions& App::Options() { return internal::OptionsRef(); }

void App::OnReady(std::function<void()> fn) { g_onReady = std::move(fn); }
void App::Post(std::function<void()> fn) { internal::PlatformPost(std::move(fn)); }
void App::Quit(int exitCode) {
    // Aviso a la app antes de empezar el cierre (app.on('before-quit') y, tras
    // cerrar ventanas, app.on('will-quit')). Se emiten ANTES de RequestQuit
    // para que el sidecar los reciba (después ya se le mata).
    ControlServer::Get().BroadcastEvent("app.event",
                                        R"({"name":"before-quit","payload":null})");
    ControlServer::Get().BroadcastEvent("app.event",
                                        R"({"name":"will-quit","payload":null})");
    internal::RequestQuit(exitCode);
}

int App::Main(int argc, char** argv, const AppOptions& options) {
    if (!internal::Bootstrap(argc, argv, options)) return 1;
    if (g_onReady) g_onReady();
    int code = internal::RunMainLoop();
    // Margen para que el sidecar lea los eventos finales (before-quit/will-quit)
    // antes de terminarlo.
#ifdef _WIN32
    Sleep(150);
#else
    usleep(150 * 1000);
#endif
    // El sidecar Node debe morir con el kernel (no dejarlo huérfano).
    NodeManager::ShutdownSidecar();
    ModuleLoader::Shutdown();
    ControlServer::Get().Stop();
    return code;
}

} // namespace ow
