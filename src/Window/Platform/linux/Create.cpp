// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Create.cpp — destructores + PCreate (GTK/WebKitGTK).
#include "Internal.hpp"
#include "PlatformData.hpp"
#include "Create/Internal.hpp"
#include "../../Window_p.hpp"
#include "../../../Core/App.hpp"
#include "../../../Core/Log.hpp"
#include "../../../Control/ControlServer.hpp"
#include "../../../Protocol/ProtocolRegistry.hpp"
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

namespace {
std::atomic<uint64_t> g_nextWindowToken{1};

/// ¿El punto (x,y) está cerca de un borde? Devuelve el borde en `edge`.
} // namespace

/// Filtro GDK: resize por borde en ventanas sin decoración. El contenido web
/// captura los eventos, así que no llegan al toplevel por bubbling; el filtro
/// se ejecuta antes de que se despachen a los widgets.
GdkFilterReturn ResizeEventFilter(GdkXEvent*, GdkEvent* event, gpointer data) {
    auto* impl = static_cast<Window::Impl*>(data);
    if (!impl->pdata || !impl->pdata->window) return GDK_FILTER_CONTINUE;
    GtkWidget* w = impl->pdata->window;
    GdkWindow* top = gtk_widget_get_window(w);
    GdkWindow* evwin = event->any.window;
    if (!top || !evwin || gdk_window_get_toplevel(evwin) != top)
        return GDK_FILTER_CONTINUE;

    // Coordenadas del evento en el espacio del toplevel.
    gint evx = 0, evy = 0, tx = 0, ty = 0;
    gdk_window_get_origin(evwin, &evx, &evy);
    gdk_window_get_origin(top, &tx, &ty);
    const double xoff = evx - tx, yoff = evy - ty;

    if (event->type == GDK_MOTION_NOTIFY) {
        GdkWindowEdge e;
        const bool near = EdgeFromPoint(
            event->motion.x + xoff, event->motion.y + yoff,
            gtk_widget_get_allocated_width(w), gtk_widget_get_allocated_height(w), e);
        const char* name = near ? CursorForEdge(e) : nullptr;
        GdkCursor* cur = name
            ? gdk_cursor_new_from_name(gdk_display_get_default(), name)
            : nullptr;
        gdk_window_set_cursor(top, cur);
        if (cur) g_object_unref(cur);
        return GDK_FILTER_CONTINUE;
    }

    if (event->type == GDK_BUTTON_PRESS && event->button.button == 1) {
        GdkWindowEdge e;
        if (!EdgeFromPoint(event->button.x + xoff, event->button.y + yoff,
                           gtk_widget_get_allocated_width(w),
                           gtk_widget_get_allocated_height(w), e))
            return GDK_FILTER_CONTINUE;
        gtk_window_begin_resize_drag(GTK_WINDOW(w), e, 1,
                                     (gint)event->button.x_root,
                                     (gint)event->button.y_root,
                                     event->button.time);
        return GDK_FILTER_REMOVE;
    }
    return GDK_FILTER_CONTINUE;
}



bool DisplayIsWayland() {
    GdkDisplay* d = gdk_display_get_default();
    const char* name = d ? G_OBJECT_TYPE_NAME(d) : nullptr;
    return name && std::string(name).find("Wayland") != std::string::npos;
}


// Fallback a nivel de ventana: el click en la webview PRINCIPAL no siempre llega
// a su button-press (lo entrega la toplevel/overlay). Si el punto no está sobre
// una hija, devolvemos el foco a la principal.
gboolean OnWindowButtonPress(GtkWidget* win, GdkEventButton* e, gpointer ud) {
    auto* impl = static_cast<Window::Impl*>(ud);
    auto* pd = impl->pdata;
    if (!pd || !impl->webview) return FALSE;
    // Click sobre una hija → le damos el foco (y la hacemos focusable).
    for (auto& [id, v] : pd->views) {
        if (!v.visible) continue;
        if (e->x >= v.x && e->x < v.x + v.w && e->y >= v.y && e->y < v.y + v.h) {
            gtk_widget_set_can_focus(v.view, TRUE);
            gtk_window_set_focus(GTK_WINDOW(win), v.view);
            gtk_widget_grab_focus(v.view);
            return FALSE;
        }
    }
    // Click fuera de las hijas → foco a la principal.
    GtkWidget* mainView = GTK_WIDGET(impl->webview->NativeWidget());
    gtk_widget_set_can_focus(mainView, TRUE);
    gtk_window_set_focus(GTK_WINDOW(win), mainView);
    gtk_widget_grab_focus(mainView);
    return FALSE;
}


Window::~Window() = default;
Window::Impl::~Impl() {
    if (pdata && pdata->resizeFilter)
        gdk_window_remove_filter(nullptr, ResizeEventFilter, this);
    alive->store(false); // callbacks diferidos (outbox/timer) dejan de tocar this
    delete pdata;
}

// ── plataforma: creación ─────────────────────────────────────────────────────
bool Window::Impl::PCreate() {
    pdata = new PlatformData();
    pdata->token = g_nextWindowToken.fetch_add(1);

    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    pdata->window = win;
    gtk_window_set_title(GTK_WINDOW(win), opts.title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(win), opts.width, opts.height);

    const bool frameless = opts.titleBarStyle != TitleBarStyle::Default;
    pdata->isWayland = DisplayIsWayland();

    ApplyFramelessChrome(this, frameless);

    pdata->overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(win), pdata->overlay);

    CreateViewContext(this);
    ApplyWindowOptions(this);
    WireWindowSignals(this);
    if (!CreateMainWebview(this, frameless)) return false;
    RegisterWindowSchemes(this);

    return true;
}

} // namespace ow
