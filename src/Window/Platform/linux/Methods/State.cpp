// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Methods/State.cpp — estado extendido (C9, GTK).
#include "../../../Window_p.hpp"
#include "../Internal.hpp"
#include "../Internal.hpp"
#include "../../../../Core/App.hpp"
#include "../../../../Core/Log.hpp"
#include "../../../../Control/ControlServer.hpp"
#include "../../../../Protocol/ProtocolRegistry.hpp"
#include "ow/detail/minjson.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace ow {

// ── plataforma: estado extendido (C9) ────────────────────────────────────────
bool Window::Impl::PIsVisible() const {
    return pdata && pdata->window && gtk_widget_get_visible(pdata->window);
}
bool Window::Impl::PIsFocused() const {
    return pdata && pdata->window && gtk_window_is_active(GTK_WINDOW(pdata->window));
}
bool Window::Impl::PIsResizable() const { return opts.resizable; }
bool Window::Impl::PIsMovable() const { return opts.movable; }
bool Window::Impl::PIsMinimizable() const { return opts.minimizable; }
bool Window::Impl::PIsMaximizable() const { return opts.maximizable; }
bool Window::Impl::PIsClosable() const { return opts.closable; }
bool Window::Impl::PIsAlwaysOnTop() const { return opts.alwaysOnTop; }
bool Window::Impl::PIsKiosk() const { return pdata && pdata->kiosk; }
bool Window::Impl::PIsDestroyed() const { return !pdata || !pdata->window; }

void Window::Impl::PSetResizable(bool on) {
    opts.resizable = on;
    if (pdata && pdata->window) gtk_window_set_resizable(GTK_WINDOW(pdata->window), on);
}
void Window::Impl::PSetMovable(bool on) { opts.movable = on; }
void Window::Impl::PSetMinimizable(bool on) { opts.minimizable = on; }
void Window::Impl::PSetMaximizable(bool on) { opts.maximizable = on; }
void Window::Impl::PSetClosable(bool on) { opts.closable = on; }
void Window::Impl::PSetAlwaysOnTop(bool on, int) {
    opts.alwaysOnTop = on;
    if (pdata && pdata->window) gtk_window_set_keep_above(GTK_WINDOW(pdata->window), on);
    EmitPlatformEvent(this, "alwaysOnTopChanged", on ? "true" : "false");
}
void Window::Impl::PSetSkipTaskbar(bool on) {
    opts.skipTaskbar = on;
    if (pdata && pdata->window)
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(pdata->window), on);
}
void Window::Impl::PSetHasShadow(bool on) { opts.hasShadow = on; }
void Window::Impl::PSetKiosk(bool on) {
    if (!pdata || !pdata->window) return;
    pdata->kiosk = on;
    if (on) gtk_window_fullscreen(GTK_WINDOW(pdata->window));
    else gtk_window_unfullscreen(GTK_WINDOW(pdata->window));
}
void Window::Impl::PSetIgnoreMouseEvents(bool ignore, bool) {
    if (!pdata || !pdata->window) return;
    GdkWindow* gw = gtk_widget_get_window(pdata->window);
    if (!gw) return;
    if (ignore) {
        cairo_rectangle_int_t empty{0, 0, 0, 0};
        cairo_region_t* r = cairo_region_create_rectangle(&empty);
        gdk_window_input_shape_combine_region(gw, r, 0, 0);
        cairo_region_destroy(r);
    } else {
        gdk_window_input_shape_combine_region(gw, nullptr, 0, 0);
    }
}
void Window::Impl::PSetProgressBar(double value, const std::string& mode) {
    // Unity LauncherEntry (D-Bus); noop si el DE no lo soporta.
    GError* err = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (!bus) {
        if (err) g_error_free(err);
        return;
    }
    const char* state = (mode == "indeterminate" || value < 0) ? "indeterminate"
                        : mode == "paused" ? "paused"
                        : mode == "error" ? "error" : "normal";
    (void)state;
    double v = value < 0 ? 0 : value;
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "progress", g_variant_new_double(v));
    g_variant_builder_add(&props, "{sv}", "progress-visible", g_variant_new_boolean(v > 0));
    g_dbus_connection_emit_signal(
        bus, nullptr, "/com/canonical/Unity/LauncherEntry",
        "com.canonical.Unity.LauncherEntry", "Update",
        g_variant_new("(sa{sv})", "application://owear.desktop", &props), nullptr);
    g_object_unref(bus);
}
void Window::Impl::PSetBackgroundColor(const std::string& color) {
    opts.backgroundColor = color;
    if (color.size() < 7 || color[0] != '#' || !webview) return;
    int r = 0, g = 0, b = 0;
    try {
        r = std::stoi(color.substr(1, 2), nullptr, 16);
        g = std::stoi(color.substr(3, 2), nullptr, 16);
        b = std::stoi(color.substr(5, 2), nullptr, 16);
    } catch (...) {
        return;
    }
    webview->SetBackgroundColor(r, g, b, 255);
}
void Window::Impl::PMoveTop() {
    if (!pdata || !pdata->window) return;
    gtk_window_present(GTK_WINDOW(pdata->window));
    if (GdkWindow* gw = gtk_widget_get_window(pdata->window)) gdk_window_raise(gw);
}
void Window::Impl::PSetAspectRatio(double ratio, int extraW, int extraH) {
    opts.aspectRatio = ratio;
    opts.aspectExtraW = extraW;
    opts.aspectExtraH = extraH;
    if (!pdata || !pdata->window || ratio <= 0) return;
    GdkGeometry geom{};
    geom.min_aspect = ratio;
    geom.max_aspect = ratio;
    gtk_window_set_geometry_hints(GTK_WINDOW(pdata->window), nullptr, &geom, GDK_HINT_ASPECT);
}
Window::Bounds Window::Impl::PGetContentBounds() const { return PGetBounds(); }
void Window::Impl::PSetContentSize(int w, int h) {
    Bounds b = PGetBounds();
    b.w = w;
    b.h = h;
    PSetBounds(b);
}
Window::Size Window::Impl::PGetContentSize() const {
    Bounds b = PGetBounds();
    return Size{b.w, b.h};
}
Window::Size Window::Impl::PGetMinimumSize() const {
    return Size{opts.minWidth, opts.minHeight};
}
Window::Size Window::Impl::PGetMaximumSize() const {
    return Size{opts.maxWidth, opts.maxHeight};
}
void Window::Impl::PSetMinimumSize(int w, int h) {
    opts.minWidth = w;
    opts.minHeight = h;
    if (pdata && pdata->window) gtk_widget_set_size_request(pdata->window, w, h);
}
void Window::Impl::PSetMaximumSize(int w, int h) {
    opts.maxWidth = w;
    opts.maxHeight = h;
}


} // namespace ow
