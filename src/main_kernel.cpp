// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/main_kernel.cpp — binario del runtime Owear.
//
// Modos:
//  1. JS-driven (OW_APP_MAIN=dist/main.js): kernel + sidecar Node (Electron-like).
//  2. Nativo-first (OW_DEMO=1): ventana demo embebida.
//
#include <ow/App.h>
#include <ow/Window.h>

#include "Core/Log.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    // stderr sin buffer: redirigido a fichero en CI, el CRT de MSVC puede
    // bufferizarlo y perder todo si el proceso muere o se cuelga.
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // Marcador de build: permite confirmar qué kernel está corriendo (los
    // logs van a stderr, y `ow dev` los hereda en la terminal).
    ow::log::Info("app", std::string("OWEAR KERNEL BUILD ") + __DATE__ + " " +
                             __TIME__);

    ow::AppOptions opts;
    if (const char* id = std::getenv("OW_APP_ID")) opts.id = id;
    if (const char* name = std::getenv("OW_APP_NAME")) opts.name = name;

    if (std::getenv("OW_DEMO")) {
        ow::App::OnReady([] {
            auto* w = new ow::Window([] {
                ow::WindowOptions wo;
                if (const char* n = std::getenv("OW_APP_NAME")) wo.title = n;
                wo.width = 1100;
                wo.height = 720;
                wo.titleBarStyle = ow::TitleBarStyle::Custom;
                // Dev: permite probar el overlay nativo sin tocar la app.
                if (std::getenv("OW_TITLEBAR_OVERLAY")) {
                    wo.titleBarOverlay.enabled = true;
                    if (const char* h = std::getenv("OW_TITLEBAR_OVERLAY_HEIGHT"))
                        wo.titleBarOverlay.height = std::atoi(h);
                }
                if (const char* url = std::getenv("OW_DEV_SERVER_URL")) wo.url = url;
                return wo;
            }());
            w->On("closed", [](ow::EventPayload) { ow::App::Quit(0); });
        });
    }

    return ow::App::Main(argc, argv, opts);
}
