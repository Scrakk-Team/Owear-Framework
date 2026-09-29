// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/window_linux.cpp — implementación GTK3 (WebKitGTK embebido).
//
// Titlebar en Linux:
//  - Default: decoraciones del gestor de ventanas.
//  - Hidden/Custom: gtk_window_set_decorated(false) + drag regions CSS
//    ([data-ow-drag]). La app dibuja sus botones.
//  - Custom + titleBarOverlay: botones min/max/close DIBUJADOS con Cairo en un
//    GtkOverlay arriba-derecha, idénticos a Electron/Chromium (ancho 45, icono
//    10px, hover 0x1A, close #E81123). El hueco se expone al web como
//    window.__owTitlebarOverlay.
//  - En Wayland el resize por bordes se hace con zonas GtkEventBox (no hay
//    eventos X para el filtro GDK).
//
#include "Window_p.hpp"
#include "Platform/linux/Internal.hpp"
#include "Platform/linux/Internal.hpp"
#include "../Core/App.hpp"
#include "../Core/Log.hpp"
#include "../Control/ControlServer.hpp"
#include "../Protocol/ProtocolRegistry.hpp"
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
// ── plataforma: ciclo de vida ────────────────────────────────────────────────
void Window::Impl::PShow() { if (pdata) gtk_widget_show(pdata->window); }
void Window::Impl::PHide() { if (pdata) gtk_widget_hide(pdata->window); }
void Window::Impl::PFocus() {
    if (pdata) {
        gtk_window_present(GTK_WINDOW(pdata->window));
    }
}
void Window::Impl::PClose() {
    if (pdata)
        g_signal_emit_by_name(pdata->window, "delete-event", nullptr, nullptr);
}
void Window::Impl::PDestroy() {
    if (pdata) gtk_widget_destroy(pdata->window);
}
void Window::Impl::PMinimize() {
    if (pdata) gtk_window_iconify(GTK_WINDOW(pdata->window));
}
void Window::Impl::PMaximize() {
    if (pdata) gtk_window_maximize(GTK_WINDOW(pdata->window));
}
void Window::Impl::PUnmaximize() {
    if (pdata) gtk_window_unmaximize(GTK_WINDOW(pdata->window));
}
void Window::Impl::PRestore() {
    if (pdata) gtk_window_deiconify(GTK_WINDOW(pdata->window));
}
void Window::Impl::PSetFullScreen(bool enabled) {
    if (!pdata) return;
    if (enabled) gtk_window_fullscreen(GTK_WINDOW(pdata->window));
    else gtk_window_unfullscreen(GTK_WINDOW(pdata->window));
}
bool Window::Impl::PIsMaximized() const {
    if (!pdata || !pdata->window) return false;
    GdkWindow* gdk = gtk_widget_get_window(pdata->window);
    return gdk && (gdk_window_get_state(gdk) & GDK_WINDOW_STATE_MAXIMIZED);
}
bool Window::Impl::PIsMinimized() const {
    if (!pdata || !pdata->window) return false;
    GdkWindow* gdk = gtk_widget_get_window(pdata->window);
    return gdk && (gdk_window_get_state(gdk) & GDK_WINDOW_STATE_ICONIFIED);
}
bool Window::Impl::PIsFullScreen() const { return pdata && pdata->fullscreen; }

// ── plataforma: geometría ────────────────────────────────────────────────────
Window::Bounds Window::Impl::PGetBounds() const {
    Bounds b;
    if (!pdata || !pdata->window) return b;
    gint w = 0, h = 0;
    gtk_window_get_size(GTK_WINDOW(pdata->window), &w, &h);
    gint x = 0, y = 0;
    gtk_window_get_position(GTK_WINDOW(pdata->window), &x, &y);
    b.x = x; b.y = y; b.w = w; b.h = h;
    return b;
}
void Window::Impl::PSetBounds(const Bounds& bounds) {
    if (!pdata || !pdata->window) return;
    gtk_window_move(GTK_WINDOW(pdata->window), bounds.x, bounds.y);
    gtk_window_resize(GTK_WINDOW(pdata->window), bounds.w, bounds.h);
}
void Window::Impl::PCenter() {
    if (pdata) gtk_window_set_position(GTK_WINDOW(pdata->window), GTK_WIN_POS_CENTER);
}

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

// ── plataforma: título ───────────────────────────────────────────────────────
void Window::Impl::PSetTitle(const std::string& t) {
    if (pdata) gtk_window_set_title(GTK_WINDOW(pdata->window), t.c_str());
}
std::string Window::Impl::PGetTitle() const {
    if (!pdata || !pdata->window) return {};
    const gchar* t = gtk_window_get_title(GTK_WINDOW(pdata->window));
    return t ? t : "";
}

// ── plataforma: drags de titlebar custom ─────────────────────────────────────
void Window::Impl::PBeginMoveDrag() {
    if (!pdata || !pdata->window) return;
    GdkWindow* gdk = gtk_widget_get_window(pdata->window);
    if (!gdk) return;
    gint rx = 0, ry = 0;
    GdkSeat* seat = nullptr;
    GdkDisplay* display = gtk_widget_get_display(pdata->window);
    if (display) {
        GdkSeat* s = gdk_display_get_default_seat(display);
        if (s) seat = s;
    }
    guint32 timestamp = GDK_CURRENT_TIME;
    if (seat) {
        GdkDevice* dev = gdk_seat_get_pointer(seat);
        if (dev) gdk_device_get_position(dev, nullptr, &rx, &ry);
    }
    gtk_window_begin_move_drag(GTK_WINDOW(pdata->window), 1, rx, ry, timestamp);
}

void Window::Impl::PBeginResizeDrag(const std::string& edge) {
    if (!pdata || !pdata->window) return;
    GdkWindow* gdk = gtk_widget_get_window(pdata->window);
    if (!gdk) return;
    GdkWindowEdge e = GDK_WINDOW_EDGE_SOUTH_EAST;
    if (edge == "left") e = GDK_WINDOW_EDGE_WEST;
    else if (edge == "right") e = GDK_WINDOW_EDGE_EAST;
    else if (edge == "top") e = GDK_WINDOW_EDGE_NORTH;
    else if (edge == "bottom") e = GDK_WINDOW_EDGE_SOUTH;
    else if (edge == "top-left") e = GDK_WINDOW_EDGE_NORTH_WEST;
    else if (edge == "top-right") e = GDK_WINDOW_EDGE_NORTH_EAST;
    else if (edge == "bottom-left") e = GDK_WINDOW_EDGE_SOUTH_WEST;
    else if (edge == "bottom-right") e = GDK_WINDOW_EDGE_SOUTH_EAST;

    gint rx = 0, ry = 0;
    GdkDisplay* display = gtk_widget_get_display(pdata->window);
    if (display) {
        GdkSeat* seat = gdk_display_get_default_seat(display);
        if (seat) {
            GdkDevice* dev = gdk_seat_get_pointer(seat);
            if (dev) gdk_device_get_position(dev, nullptr, &rx, &ry);
        }
    }
    gtk_window_begin_resize_drag(GTK_WINDOW(pdata->window), e, 1, rx, ry,
                                 GDK_CURRENT_TIME);
}

} // namespace ow
