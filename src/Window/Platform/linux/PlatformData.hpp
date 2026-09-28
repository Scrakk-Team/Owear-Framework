// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/PlatformData.hpp — datos de plataforma (GTK) de Window.
#pragma once
#include "ow/Window.h"
#include "../../Window_p.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstdint>
#include <map>

namespace ow {

struct Window::Impl::PlatformData {
    GtkWidget* window = nullptr;
    GtkWidget* overlay = nullptr;     // GtkOverlay contenedor (si overlay activo)
    GtkWidget* overlayBar = nullptr;  // GtkBox con los botones del tema
    GtkWidget* overlayMaxBtn = nullptr;
    GtkCssProvider* cssProvider = nullptr;  // colores del overlay (a nivel screen)
    bool webviewReady = false;
    bool isWayland = false;
    bool fullscreen = false;
    bool resizeFilter = false;
    bool kiosk = false;
    uint64_t token = 0;

    // ── webviews embebidas (hijas de la ventana) ────────────────────────
    struct EmbeddedView {
        GtkWidget* box = nullptr;   // contenedor overlay (GtkEventBox) de la hija
        GtkWidget* view = nullptr;  // WebKitWebView
        int x = 0, y = 0, w = 0, h = 0;
        bool visible = true;
    };
    WebKitWebContext* viewCtx = nullptr;     // contexto compartido de las hijas
    std::map<uint32_t, EmbeddedView> views;  // id → webview embebida
    uint32_t nextViewId = 1;
};
} // namespace ow
