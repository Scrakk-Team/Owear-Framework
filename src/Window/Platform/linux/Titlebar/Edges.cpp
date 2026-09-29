// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Titlebar.cpp — overlay nativo + resize por bordes.
#include "../Internal.hpp"
#include "../PlatformData.hpp"

#include <gtk/gtk.h>

#include <string>


namespace ow {

// ── resize por bordes (Wayland) ──────────────────────────────────────────────
// En Wayland no hay eventos X, así que el filtro GDK no sirve. Ponemos
// "zonas" invisibles (GtkEventBox) en bordes/esquinas dentro del overlay que
// disparan gtk_window_begin_resize_drag (funciona en Wayland y X11).
struct ResizeCtx {
    Window::Impl* impl;
    GdkWindowEdge edge;
};

gboolean OnEdgePress(GtkWidget*, GdkEventButton* e, gpointer ud) {
    auto* ctx = static_cast<ResizeCtx*>(ud);
    if (e->button != 1 || !ctx->impl->pdata || !ctx->impl->pdata->window)
        return FALSE;
    gtk_window_begin_resize_drag(GTK_WINDOW(ctx->impl->pdata->window), ctx->edge,
                                 1, static_cast<gint>(e->x_root),
                                 static_cast<gint>(e->y_root), e->time);
    return TRUE;
}

gboolean OnEdgeEnter(GtkWidget* w, GdkEventCrossing*, gpointer ud) {
    auto* ctx = static_cast<ResizeCtx*>(ud);
    GdkWindow* gw = gtk_widget_get_window(w);
    if (!gw) return FALSE;
    if (const char* name = CursorForEdge(ctx->edge)) {
        GdkCursor* c = gdk_cursor_new_from_name(gdk_window_get_display(gw), name);
        gdk_window_set_cursor(gw, c);
        if (c) g_object_unref(c);
    }
    return FALSE;
}

gboolean OnEdgeLeave(GtkWidget* w, GdkEventCrossing*, gpointer) {
    if (GdkWindow* gw = gtk_widget_get_window(w)) gdk_window_set_cursor(gw, nullptr);
    return FALSE;
}

void AddResizeEdge(Window::Impl* impl, GdkWindowEdge edge, GtkAlign ha, GtkAlign va,
                   int w, int h) {
    auto* pd = impl->pdata;
    GtkWidget* eb = gtk_event_box_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(eb), "ow-resize-edge");
    gtk_widget_set_halign(eb, ha);
    gtk_widget_set_valign(eb, va);
    gtk_widget_set_size_request(eb, w, h);
    gtk_widget_add_events(eb, GDK_BUTTON_PRESS_MASK | GDK_ENTER_NOTIFY_MASK |
                                  GDK_LEAVE_NOTIFY_MASK);
    GtkCssProvider* p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p, ".ow-resize-edge{background:transparent;}",
                                    -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(eb),
                                   GTK_STYLE_PROVIDER(p),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);

    auto* ctx = new ResizeCtx{impl, edge};
    g_signal_connect_data(
        eb, "button-press-event", G_CALLBACK(OnEdgePress), ctx,
        +[](gpointer d, GClosure*) { delete static_cast<ResizeCtx*>(d); },
        GConnectFlags(0));
    g_signal_connect(eb, "enter-notify-event", G_CALLBACK(OnEdgeEnter), ctx);
    g_signal_connect(eb, "leave-notify-event", G_CALLBACK(OnEdgeLeave), ctx);

    gtk_overlay_add_overlay(GTK_OVERLAY(pd->overlay), eb);
    gtk_widget_show(eb);
}

void BuildResizeEdges(Window::Impl* impl) {
    auto* pd = impl->pdata;
    if (!pd || !pd->overlay || !impl->opts.resizable) return;
    const int B = 6;   // grosor del borde
    const int C = 14;  // tamaño de las esquinas
    AddResizeEdge(impl, GDK_WINDOW_EDGE_NORTH, GTK_ALIGN_FILL, GTK_ALIGN_START, -1, B);
    AddResizeEdge(impl, GDK_WINDOW_EDGE_SOUTH, GTK_ALIGN_FILL, GTK_ALIGN_END, -1, B);
    AddResizeEdge(impl, GDK_WINDOW_EDGE_WEST, GTK_ALIGN_START, GTK_ALIGN_FILL, B, -1);
    AddResizeEdge(impl, GDK_WINDOW_EDGE_EAST, GTK_ALIGN_END, GTK_ALIGN_FILL, B, -1);
    AddResizeEdge(impl, GDK_WINDOW_EDGE_NORTH_WEST, GTK_ALIGN_START, GTK_ALIGN_START, C, C);
    AddResizeEdge(impl, GDK_WINDOW_EDGE_NORTH_EAST, GTK_ALIGN_END, GTK_ALIGN_START, C, C);
    AddResizeEdge(impl, GDK_WINDOW_EDGE_SOUTH_WEST, GTK_ALIGN_START, GTK_ALIGN_END, C, C);
    AddResizeEdge(impl, GDK_WINDOW_EDGE_SOUTH_EAST, GTK_ALIGN_END, GTK_ALIGN_END, C, C);
}

/// ¿El display es Wayland? (ahí el filtro XEvent no sirve para resize).


} // namespace ow
