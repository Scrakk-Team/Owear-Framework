// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Titlebar.cpp — overlay nativo + resize por bordes.
#include "Internal.hpp"
#include "PlatformData.hpp"

#include <gtk/gtk.h>

#include <string>

namespace ow {

bool EdgeFromPoint(double x, double y, int w, int h, GdkWindowEdge& edge) {
    const double m = 5.0;
    const bool left = x <= m, right = x >= w - m;
    const bool top = y <= m, bottom = y >= h - m;
    if (top && left) { edge = GDK_WINDOW_EDGE_NORTH_WEST; return true; }
    if (top && right) { edge = GDK_WINDOW_EDGE_NORTH_EAST; return true; }
    if (bottom && left) { edge = GDK_WINDOW_EDGE_SOUTH_WEST; return true; }
    if (bottom && right) { edge = GDK_WINDOW_EDGE_SOUTH_EAST; return true; }
    if (top) { edge = GDK_WINDOW_EDGE_NORTH; return true; }
    if (bottom) { edge = GDK_WINDOW_EDGE_SOUTH; return true; }
    if (left) { edge = GDK_WINDOW_EDGE_WEST; return true; }
    if (right) { edge = GDK_WINDOW_EDGE_EAST; return true; }
    return false;
}

const char* CursorForEdge(GdkWindowEdge e) {
    switch (e) {
    case GDK_WINDOW_EDGE_NORTH_WEST: return "nw-resize";
    case GDK_WINDOW_EDGE_NORTH:      return "n-resize";
    case GDK_WINDOW_EDGE_NORTH_EAST: return "ne-resize";
    case GDK_WINDOW_EDGE_WEST:       return "w-resize";
    case GDK_WINDOW_EDGE_EAST:       return "e-resize";
    case GDK_WINDOW_EDGE_SOUTH_WEST: return "sw-resize";
    case GDK_WINDOW_EDGE_SOUTH:      return "s-resize";
    case GDK_WINDOW_EDGE_SOUTH_EAST: return "se-resize";
    }
    return nullptr;
}


// ── plataforma: titlebar ─────────────────────────────────────────────────────
void Window::Impl::PSetApplicationMenu(const std::string&) {
    // noop por diseño: en GNOME/Linux no imponemos menubar (el IDE dibuja el suyo).
}

void Window::Impl::PSetColorScheme(int) {
    // noop: WebKitGTK no expone forzar el color-scheme (el tema lo define). La
    // app reacciona a `theme.changed` para su propio theming.
}

void Window::Impl::PPrintToPDF(std::function<void(bool, const std::string&)> cb) {
    if (webview) webview->PrintToPDF(std::move(cb));
    else if (cb) cb(false, {});
}

void Window::Impl::PApplyTitleBar() {
    if (!pdata || !pdata->window) return;
    switch (opts.titleBarStyle) {
    case TitleBarStyle::Default:
        gtk_window_set_decorated(GTK_WINDOW(pdata->window), TRUE);
        break;
    case TitleBarStyle::Hidden:
    case TitleBarStyle::Custom:
        // No hacemos set_decorated(FALSE): usamos CSD con un titlebar vacío
        // (ver PCreate) para conservar la SOMBRA del tema. Desactivar la
        // decoración apagaría el CSD.
        break;
    }
    // En PCreate la barra se construye al final (cuando el webview ya existe
    // para poder inyectar el script); aquí solo gestionamos cambios en caliente.
    if (!pdata->webviewReady) return;
    if (opts.titleBarOverlay.enabled) BuildOverlayBar(this);
    else DestroyOverlayBar(this);
}



} // namespace ow
