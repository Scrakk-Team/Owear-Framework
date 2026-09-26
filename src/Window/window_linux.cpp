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
#include "../Core/Log.hpp"
#include "ow/detail/minjson.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

namespace ow {

namespace {
std::atomic<uint64_t> g_nextWindowToken{1};

/// ¿El punto (x,y) está cerca de un borde? Devuelve el borde en `edge`.
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
} // namespace

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
    uint64_t token = 0;
};

namespace {

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

} // namespace

namespace {

// ── overlay nativo (titleBarOverlay) ─────────────────────────────────────────
// Usamos los botones de ventana DEL TEMA (GTK `titlebutton`, los mismos que
// pinta GtkHeaderBar) para que el estilo, el tamaño y el espaciado los defina
// la distro. Nada de métricas hardcodeadas. Iconos simbólicos estándar de
// freedesktop (los mismos nombres que usa Electron en Linux).

/// "#RGB"/"#RRGGBB"/"#RRGGBBAA" → "#rrggbb" (o "" si transparente/inválido).
std::string CssHex(const std::string& in) {
    std::string s = in;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.size() == 3) s = {s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() != 6 && s.size() != 8) return {};
    if (s.size() == 8 && std::strtoul(s.substr(6, 2).c_str(), nullptr, 16) == 0)
        return {}; // alpha 0 ⇒ dejar transparente
    return "#" + s.substr(0, 6);
}

/// "#rrggbb" → "#000000"/"#ffffff" según luminancia (contraste legible).
std::string ContrastHex(const std::string& hex) {
    if (hex.size() != 7) return "#ffffff";
    auto hx = [&](int i) {
        return std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16) / 255.0;
    };
    const double lum = 0.2126 * hx(1) + 0.7152 * hx(3) + 0.0722 * hx(5);
    return lum > 0.5 ? "#000000" : "#ffffff";
}

/// Aclara (amt>0) u oscurece (amt<0) un "#rrggbb".
std::string ShadeHex(const std::string& hex, double amt) {
    if (hex.size() != 7) return hex;
    auto hx = [&](int i) {
        return static_cast<int>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16));
    };
    auto adj = [amt](int v) {
        double x = v / 255.0;
        x = amt >= 0 ? x + (1.0 - x) * amt : x * (1.0 + amt);
        int r = static_cast<int>(x * 255.0 + 0.5);
        return r < 0 ? 0 : (r > 255 ? 255 : r);
    };
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", adj(hx(1)), adj(hx(3)),
                  adj(hx(5)));
    return buf;
}

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
bool DisplayIsWayland() {
    GdkDisplay* d = gdk_display_get_default();
    const char* name = d ? G_OBJECT_TYPE_NAME(d) : nullptr;
    return name && std::string(name).find("Wayland") != std::string::npos;
}

} // namespace

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

    // Esquinas redondeadas (Linux): ventana con visual RGBA y fondo
    // transparente. La superficie visible la pinta el contenido web, que
    // redondeamos por CSS (ver más abajo). En Wayland no existe el shaping,
    // así que se hace con alpha + overflow del propio HTML.
    if (frameless) {
        gtk_widget_set_app_paintable(win, TRUE);
        if (GdkScreen* screen = gtk_widget_get_screen(win)) {
            if (GdkVisual* vis = gdk_screen_get_rgba_visual(screen)) {
                gtk_widget_set_visual(win, vis);
                log::Debug("window", "frameless: visual RGBA disponible");
            } else {
                log::Warn("window",
                          "frameless: sin visual RGBA (bordes redondeados pueden "
                          "no verse)");
            }
        }
        GtkCssProvider* css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(
            css,
            "window, overlay { background-color: transparent; }", -1, nullptr);
        gtk_style_context_add_provider(gtk_widget_get_style_context(win),
                                       GTK_STYLE_PROVIDER(css),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
    }

    // GtkOverlay como contenedor: sirve para (a) superponer los botones nativos
    // (titleBarOverlay) y (b) las zonas de resize cuando estamos en Wayland.
    if (frameless && (opts.titleBarOverlay.enabled || pdata->isWayland)) {
        pdata->overlay = gtk_overlay_new();
        gtk_container_add(GTK_CONTAINER(win), pdata->overlay);
    }

    if (!opts.resizable) gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    if (opts.minWidth > 0 || opts.minHeight > 0)
        gtk_window_set_geometry_hints(GTK_WINDOW(win), nullptr, nullptr, GdkWindowHints(0));

    PApplyTitleBar();

    g_signal_connect(win, "delete-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer user_data) -> gboolean {
                         // F3.4: el flujo central decide (veto nativo → JS → timeout)
                         static_cast<Window::Impl*>(user_data)->BeginCloseFlow();
                         return TRUE; // siempre bloqueamos; Destroy() lo cierra
                     }),
                     this);

    g_signal_connect(win, "destroy",
                     G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
                         auto* impl = static_cast<Window::Impl*>(user_data);
                         Window::Impl::EmitPlatformEvent(impl, "closed");
                     }),
                     this);

    g_signal_connect(win, "configure-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventConfigure* e, gpointer user_data) -> gboolean {
                         auto* impl = static_cast<Window::Impl*>(user_data);
                         json::Object o;
                         o.emplace_back("x", json::Value(e->x));
                         o.emplace_back("y", json::Value(e->y));
                         o.emplace_back("width", json::Value(e->width));
                         o.emplace_back("height", json::Value(e->height));
                         Window::Impl::EmitPlatformEvent(impl, "resize",
                                         json::Value(std::move(o)).Serialize());
                         return FALSE;
                     }),
                     this);

    g_signal_connect(win, "window-state-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventWindowState* e, gpointer user_data) -> gboolean {
                         auto* impl = static_cast<Window::Impl*>(user_data);
                         bool maximized = e->new_window_state & GDK_WINDOW_STATE_MAXIMIZED;
                         bool fullscreen = e->new_window_state & GDK_WINDOW_STATE_FULLSCREEN;
                         impl->pdata->fullscreen = fullscreen;
                         UpdateOverlayMaxIcon(impl, maximized);
                         Window::Impl::EmitPlatformEvent(impl, maximized ? "maximize" : "unmaximize");
                         Window::Impl::EmitPlatformEvent(impl, fullscreen ? "enterFullScreen" : "leaveFullScreen");
                         return FALSE;
                     }),
                     this);

    g_signal_connect(win, "focus-in-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer user_data) -> gboolean {
                         Window::Impl::EmitPlatformEvent(static_cast<Window::Impl*>(user_data), "focus");
                         return FALSE;
                     }),
                     this);
    g_signal_connect(win, "focus-out-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer user_data) -> gboolean {
                         Window::Impl::EmitPlatformEvent(static_cast<Window::Impl*>(user_data), "blur");
                         return FALSE;
                     }),
                     this);

    gtk_widget_show_all(win);

    // Con overlay, el webview es el child principal del GtkOverlay.
    GtkWidget* contentParent = pdata->overlay ? pdata->overlay : win;
    if (!webview->Create(contentParent, opts.webviewArgs)) return false;
    pdata->webviewReady = true;

    // ── eventos de navegación (siempre activos) ────────────────────────
    GtkWidget* view = GTK_WIDGET(webview->NativeWidget());

    if (frameless) {
        // WebView transparente: el fondo lo pone el HTML redondeado y las
        // esquinas dejan ver el escritorio (ventana RGBA).
        if (WEBKIT_IS_WEB_VIEW(view)) {
            GdkRGBA transparent = {0, 0, 0, 0};
            webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(view), &transparent);
            if (WebKitUserContentManager* ucm =
                    webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(view))) {
                static const char* kRoundCss =
                    "html{background:transparent!important}"
                    "body{border-radius:10px!important;overflow:hidden!important}";
                WebKitUserStyleSheet* ss = webkit_user_style_sheet_new(
                    kRoundCss, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                    WEBKIT_USER_STYLE_LEVEL_USER, nullptr, nullptr);
                webkit_user_content_manager_add_style_sheet(ucm, ss);
                webkit_user_style_sheet_unref(ss);
            }
        }

        // Resize sin decoración: filtro GDK (el WebView captura los eventos,
        // así que no llegan al toplevel por bubbling).
        if (opts.resizable) {
            pdata->resizeFilter = true;
            gdk_window_add_filter(nullptr, ResizeEventFilter, this);
        }
    }

    if (WEBKIT_IS_WEB_VIEW(view)) {
        g_signal_connect(view, "load-changed",
            G_CALLBACK(+[](WebKitWebView* v, WebKitLoadEvent ev, gpointer ud) {
                auto* impl = static_cast<Window::Impl*>(ud);
                const char* name = nullptr;
                switch (ev) {
                case WEBKIT_LOAD_STARTED: name = "navigationStarted"; break;
                case WEBKIT_LOAD_COMMITTED: name = "loadCommitted"; break;
                case WEBKIT_LOAD_FINISHED: name = "didFinishLoad"; break;
                default: return;
                }
                if (ev == WEBKIT_LOAD_STARTED) {
                    const gchar* u = webkit_web_view_get_uri(v);
                    log::Debug("nav", std::string("STARTED: ") + (u ? u : "?"));
                }
                Window::Impl::EmitPlatformEvent(impl, name);
            }), this);
        g_signal_connect(view, "load-failed",
            G_CALLBACK(+[](WebKitWebView* v, WebKitLoadEvent, gchar* failing_uri,
                           GError* err, gpointer ud) -> gboolean {
                auto* impl = static_cast<Window::Impl*>(ud);
                json::Object o;
                o.emplace_back("url", json::Value(std::string(
                                          failing_uri ? failing_uri : "")));
                o.emplace_back("code",
                               json::Value(static_cast<int64_t>(err ? err->code : 0)));
                o.emplace_back("description", json::Value(std::string(
                                                  err ? err->message : "")));
                Window::Impl::EmitPlatformEvent(impl, "didFailLoad",
                    json::Value(std::move(o)).Serialize());
                return FALSE; // deja que WebKit muestre su página de error
            }), this);
        g_object_bind_property(view, "title", win, "title", G_BINDING_DEFAULT);
        g_signal_connect(view, "notify::title",
            G_CALLBACK(+[](WebKitWebView* v, GParamSpec*, gpointer ud) {
                auto* impl = static_cast<Window::Impl*>(ud);
                const gchar* t = webkit_web_view_get_title(v);
                json::Object o;
                o.emplace_back("title", json::Value(std::string(t ? t : "")));
                Window::Impl::EmitPlatformEvent(impl, "pageTitleUpdated",
                    json::Value(std::move(o)).Serialize());
            }), this);
    }

    // scheme app:// → sirve archivos del directorio de assets si existe
    const char* assetsDir = std::getenv("OW_ASSETS_DIR");
    if (assetsDir && *assetsDir)
        webview->RegisterAssetScheme("app", std::filesystem::path(assetsDir));
    else
        webview->RegisterAssetScheme("app", std::filesystem::current_path() / "dist");

    if (pdata->overlay) {
        if (pdata->isWayland) BuildResizeEdges(this);
        BuildOverlayBar(this);
    }

    return true;
}

// ── plataforma: titlebar ─────────────────────────────────────────────────────
void Window::Impl::PApplyTitleBar() {
    if (!pdata || !pdata->window) return;
    switch (opts.titleBarStyle) {
    case TitleBarStyle::Default:
        gtk_window_set_decorated(GTK_WINDOW(pdata->window), TRUE);
        break;
    case TitleBarStyle::Hidden:
    case TitleBarStyle::Custom:
        gtk_window_set_decorated(GTK_WINDOW(pdata->window), FALSE);
        break;
    }
    // En PCreate la barra se construye al final (cuando el webview ya existe
    // para poder inyectar el script); aquí solo gestionamos cambios en caliente.
    if (!pdata->webviewReady) return;
    if (opts.titleBarOverlay.enabled) BuildOverlayBar(this);
    else DestroyOverlayBar(this);
}

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
