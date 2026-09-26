// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/window_linux.cpp — implementación GTK3 (WebKitGTK embebido).
//
// Titlebar en Linux:
//  - Default: decoraciones del gestor de ventanas.
//  - Hidden/Custom: gtk_window_set_decorated(false) + drag regions CSS
//    ([data-ow-drag]). Los botones overlay nativos son exclusivos de
//    Windows/macOS; en Linux la app dibuja los suyos.
//
#include "Window_p.hpp"
#include "../Core/Log.hpp"
#include "ow/detail/minjson.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <map>

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

    // Esquinas redondeadas (Linux): ventana con visual RGBA y fondo
    // transparente. La superficie visible la pinta el contenido web, que
    // redondeamos por CSS (ver más abajo). En Wayland no existe el shaping,
    // así que se hace con alpha + overflow del propio HTML.
    if (frameless) {
        gtk_widget_set_app_paintable(win, TRUE);
        if (GdkScreen* screen = gtk_widget_get_screen(win)) {
            if (GdkVisual* vis = gdk_screen_get_rgba_visual(screen))
                gtk_widget_set_visual(win, vis);
        }
        GtkCssProvider* css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(
            css, "window { background-color: transparent; }", -1, nullptr);
        gtk_style_context_add_provider(gtk_widget_get_style_context(win),
                                       GTK_STYLE_PROVIDER(css),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
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

    if (!webview->Create(win, opts.webviewArgs)) return false;

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
        // Custom == Hidden + drag regions del lado web. Overlay nativo es
        // Win/Mac only (documentado).
        gtk_window_set_decorated(GTK_WINDOW(pdata->window), FALSE);
        break;
    }
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
