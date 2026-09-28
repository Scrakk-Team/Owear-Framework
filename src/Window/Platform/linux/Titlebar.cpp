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

// ── overlay nativo (titleBarOverlay) ─────────────────────────────────────────
// Usamos los botones de ventana DEL TEMA (GTK `titlebutton`, los mismos que
// pinta GtkHeaderBar) para que el estilo, el tamaño y el espaciado los defina
// la distro. Nada de métricas hardcodeadas. Iconos simbólicos estándar de
// freedesktop (los mismos nombres que usa Electron en Linux).

void OnTbMinimize(GtkButton*, gpointer ud) {
    auto* impl = static_cast<Window::Impl*>(ud);
    if (impl->pdata && impl->pdata->window)
        gtk_window_iconify(GTK_WINDOW(impl->pdata->window));
}

void OnTbMaximize(GtkButton*, gpointer ud) {
    auto* impl = static_cast<Window::Impl*>(ud);
    if (!impl->pdata || !impl->pdata->window) return;
    GtkWindow* w = GTK_WINDOW(impl->pdata->window);
    if (gtk_window_is_maximized(w)) gtk_window_unmaximize(w);
    else gtk_window_maximize(w);
}

void OnTbClose(GtkButton*, gpointer ud) {
    static_cast<Window::Impl*>(ud)->BeginCloseFlow();
}

/// Cambia el icono del botón maximizar según el estado (nombre estándar).
void UpdateOverlayMaxIcon(Window::Impl* impl, bool maximized) {
    auto* pd = impl->pdata;
    if (!pd || !pd->overlayMaxBtn) return;
    GtkWidget* img = gtk_button_get_image(GTK_BUTTON(pd->overlayMaxBtn));
    if (img)
        gtk_image_set_from_icon_name(
            GTK_IMAGE(img),
            maximized ? "window-restore-symbolic" : "window-maximize-symbolic",
            GTK_ICON_SIZE_BUTTON);
}

/// Crea (idempotente) la barra con los botones del tema dentro del GtkOverlay.
void BuildOverlayBar(Window::Impl* impl) {
    auto* pd = impl->pdata;
    if (!pd || !pd->overlay || pd->overlayBar) return;
    if (!impl->opts.titleBarOverlay.enabled) return;

    const int h = impl->opts.titleBarOverlay.height > 0
                      ? impl->opts.titleBarOverlay.height
                      : 32;

    // GtkBox transparente del alto de banda (`height`). Los botones NO se
    // estiran: conservan su tamaño natural y se centran verticalmente, así el
    // estilo lo decide el tema de la distro.
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(box, "ow-titlebar-overlay"); // id para ganar especificidad
    gtk_widget_set_halign(box, GTK_ALIGN_END);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_size_request(box, -1, h);

    GtkWidget* minb =
        gtk_button_new_from_icon_name("window-minimize-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* maxb =
        gtk_button_new_from_icon_name("window-maximize-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* closeb =
        gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btns[3] = {minb, maxb, closeb};
    for (GtkWidget* b : btns) {
        gtk_style_context_add_class(gtk_widget_get_style_context(b), "titlebutton");
        gtk_widget_set_valign(b, GTK_ALIGN_CENTER); // tamaño natural del tema
        gtk_widget_set_can_focus(b, FALSE);
        gtk_widget_set_focus_on_click(b, FALSE);
        gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
    }
    gtk_style_context_add_class(gtk_widget_get_style_context(closeb), "close");

    g_signal_connect(minb, "clicked", G_CALLBACK(OnTbMinimize), impl);
    g_signal_connect(maxb, "clicked", G_CALLBACK(OnTbMaximize), impl);
    g_signal_connect(closeb, "clicked", G_CALLBACK(OnTbClose), impl);

    // Colores (todos opcionales; por defecto los del tema). El selector va por
    // id (`#ow-titlebar-overlay`) para ganar en especificidad a las reglas
    // `button.titlebutton` del tema.
    //  - color: fondo de la banda.
    //  - symbolColor: glifo.
    //  - buttonColor: fondo INTERNO del círculo (hover/pressed derivados).
    const std::string bg = CssHex(impl->opts.titleBarOverlay.color);
    const std::string btn = CssHex(impl->opts.titleBarOverlay.buttonColor);
    std::string fg = impl->opts.titleBarOverlay.symbolColor;
    if (fg.empty() && !btn.empty()) fg = ContrastHex(btn);
    else if (fg.empty() && !bg.empty()) fg = ContrastHex(bg);

    std::string css =
        "#ow-titlebar-overlay{background:transparent;box-shadow:none;border:none;}";
    if (!bg.empty()) css += "#ow-titlebar-overlay{background:" + bg + ";}";
    if (!fg.empty())
        css += "#ow-titlebar-overlay button.titlebutton{color:" + fg + ";}";
    if (!btn.empty()) {
        css += "#ow-titlebar-overlay button.titlebutton{background-image:none;"
               "background-color:" + btn + ";}";
        css += "#ow-titlebar-overlay button.titlebutton:hover{background-image:none;"
               "background-color:" + ShadeHex(btn, 0.08) + ";}";
        css += "#ow-titlebar-overlay button.titlebutton:active{background-image:none;"
               "background-color:" + ShadeHex(btn, -0.12) + ";}";
    }
    GtkCssProvider* prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, css.c_str(), -1, nullptr);
    // OJO: gtk_style_context_add_provider() en GTK3 solo afecta a ESE widget,
    // no a sus hijos. Los botones son hijos → hay que registrarlo a nivel de
    // pantalla (los selectores #id ya lo acotan).
    if (pd->cssProvider) {
        gtk_style_context_remove_provider_for_screen(
            gdk_screen_get_default(), GTK_STYLE_PROVIDER(pd->cssProvider));
        g_object_unref(pd->cssProvider);
    }
    pd->cssProvider = prov;
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    gtk_overlay_add_overlay(GTK_OVERLAY(pd->overlay), box);
    gtk_widget_show_all(box);

    pd->overlayBar = box;
    pd->overlayMaxBtn = maxb;
    UpdateOverlayMaxIcon(impl, GTK_WINDOW(pd->window) &&
                                   gtk_window_is_maximized(GTK_WINDOW(pd->window)));

    // Hueco reservado al renderer (el ancho lo mide GTK con el tema real).
    if (pd->webviewReady && impl->webview) {
        GtkRequisition req{};
        gtk_widget_get_preferred_size(box, nullptr, &req);
        const int w = req.width > 0 ? req.width : h * 3;
        impl->webview->InjectInitScript(
            "window.__owTitlebarOverlay={enabled:true,height:" +
            std::to_string(h) + ",width:" + std::to_string(w) +
            ",top:0,right:0};");
    }
}

void DestroyOverlayBar(Window::Impl* impl) {
    auto* pd = impl->pdata;
    if (!pd) return;
    if (pd->cssProvider) {
        gtk_style_context_remove_provider_for_screen(
            gdk_screen_get_default(), GTK_STYLE_PROVIDER(pd->cssProvider));
        g_object_unref(pd->cssProvider);
        pd->cssProvider = nullptr;
    }
    if (!pd->overlayBar) return;
    gtk_widget_destroy(pd->overlayBar);
    pd->overlayBar = nullptr;
    pd->overlayMaxBtn = nullptr;
}

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
