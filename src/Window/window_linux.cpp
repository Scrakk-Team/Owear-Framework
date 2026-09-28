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

namespace {
std::atomic<uint64_t> g_nextWindowToken{1};

/// ¿El punto (x,y) está cerca de un borde? Devuelve el borde en `edge`.
} // namespace



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

    // CSD (client-side decorations) con un titlebar vacío de alto 0: así GTK
    // dibuja la SOMBRA del tema del escritorio (y sus esquinas) alrededor de la
    // ventana, sin quitar contenido. Es lo que hace Electron en Linux. El
    // fondo sigue siendo transparente para que mande el contenido web.
    if (frameless) {
        GtkWidget* tb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_name(tb, "ow-titlebar");
        gtk_window_set_titlebar(GTK_WINDOW(win), tb);
        // GTK le pone la clase .titlebar del tema (con su min-height) → dejaba
        // un "hueco fantasma" arriba. Provider en el propio titlebar (un
        // provider en la ventana NO afecta a los hijos) para anularlo.
        GtkCssProvider* tbcss = gtk_css_provider_new();
        gtk_css_provider_load_from_data(
            tbcss,
            "#ow-titlebar { min-height: 0; padding: 0; margin: 0; border: 0;"
            " background: transparent; }",
            -1, nullptr);
        gtk_style_context_add_provider(gtk_widget_get_style_context(tb),
                                       GTK_STYLE_PROVIDER(tbcss),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(tbcss);
    }

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
            css, "window, overlay { background-color: transparent; }", -1,
            nullptr);
        gtk_style_context_add_provider(gtk_widget_get_style_context(win),
                                       GTK_STYLE_PROVIDER(css),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
    }

    // GtkOverlay SIEMPRE como contenedor: (a) botones nativos (titleBarOverlay),
    // (b) zonas de resize en Wayland, (c) webviews embebidas (hijas).
    pdata->overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(win), pdata->overlay);

    // Contexto WebKit compartido para las webviews embebidas (data dir por app).
    {
        const std::string d = ViewDataDir("data");
        const std::string c = ViewDataDir("cache");
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        std::filesystem::create_directories(c, ec);
        WebKitWebsiteDataManager* dm = webkit_website_data_manager_new(
            "base-data-directory", d.c_str(), "base-cache-directory", c.c_str(),
            nullptr);
        pdata->viewCtx = webkit_web_context_new_with_website_data_manager(dm);
        // Un WebProcess por vista (como un "hijo CEF" pero con el motor del SO).
        webkit_web_context_set_process_model(
            pdata->viewCtx, WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES);
    }

    gtk_window_set_resizable(GTK_WINDOW(win), opts.resizable);
    if (opts.skipTaskbar) gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
    if (opts.alwaysOnTop) gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
    if (opts.parent) {
        auto it = LiveWindows().find(opts.parent);
        if (it != LiveWindows().end()) {
            GtkWindow* pw = GTK_WINDOW(
                gtk_widget_get_toplevel(GTK_WIDGET(it->second->NativeHandle())));
            if (pw && GTK_IS_WINDOW(pw)) {
                gtk_window_set_transient_for(GTK_WINDOW(win), pw);
                if (opts.modal) gtk_window_set_modal(GTK_WINDOW(win), TRUE);
            }
        }
    }
    if (opts.aspectRatio > 0.0) {
        GdkGeometry geom{};
        geom.min_aspect = geom.max_aspect = opts.aspectRatio;
        gtk_window_set_geometry_hints(GTK_WINDOW(win), nullptr, &geom, GDK_HINT_ASPECT);
    }
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

    // Foco en click sobre la ventana (fallback para la webview principal).
    g_signal_connect(win, "button-press-event",
                     G_CALLBACK(OnWindowButtonPress), this);

    gtk_widget_show_all(win);

    // Con overlay, el webview es el child principal del GtkOverlay.
    GtkWidget* contentParent = pdata->overlay ? pdata->overlay : win;
    std::vector<std::string> wargs = opts.webviewArgs;
    for (const auto& a : internal::CommandArgs()) wargs.push_back(a);
    if (!webview->Create(contentParent, WebviewArgsWithSession(wargs, opts.session)))
        return false;
    webview->SetEventSink([this](const std::string& name, std::string_view json) {
        Window::Impl::EmitPlatformEvent(this, name, json);
    });
    pdata->webviewReady = true;

    // ── eventos de navegación (siempre activos) ────────────────────────
    GtkWidget* view = GTK_WIDGET(webview->NativeWidget());

    // Foco en click en la webview PRINCIPAL: recupera el foco al pulsarla
    // (sin esto, tras usar una hija el teclado se quedaba en la hija).
    gtk_widget_set_can_focus(view, TRUE);
    g_signal_connect(view, "button-press-event", G_CALLBACK(OnViewButtonPress),
                     nullptr);
    gtk_widget_grab_focus(view); // la principal arranca con el foco

    if (frameless) {
        // WebView transparente: el fondo lo pone el HTML redondeado y las
        // esquinas dejan ver el escritorio (ventana RGBA).
        if (WEBKIT_IS_WEB_VIEW(view)) {
            GdkRGBA transparent = {0, 0, 0, 0};
            webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(view), &transparent);
            if (WebKitUserContentManager* ucm =
                    webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(view))) {
                // Redondeo del contenido web. OJO: el fondo del `body` se
                // propaga al canvas (html transparente) y se pinta CUADRADO,
                // así que el `border-radius` del body no basta: hay que clipear
                // la raíz con `clip-path`. Si no, las esquinas web (cuadradas)
                // tapan las esquinas redondeadas nativas del tema.
                // Radio del tema (NO hardcodeado): se lee el
                // `decoration { border-radius: N }` del CSS del tema activo.
                // GTK no lo expone por API, así que se parsea con fallback 10.
                const int radius = ThemeWindowRadius();
                const std::string r = std::to_string(radius);
                const std::string roundCss =
                    "html{background:transparent!important;"
                    "clip-path:inset(0 round " + r + "px)!important}"
                    "body{border-radius:" + r + "px!important;"
                    "overflow:hidden!important}";
                WebKitUserStyleSheet* ss = webkit_user_style_sheet_new(
                    roundCss.c_str(), WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                    WEBKIT_USER_STYLE_LEVEL_USER, nullptr, nullptr);
                webkit_user_content_manager_add_style_sheet(ucm, ss);
                webkit_user_style_sheet_unref(ss);
                log::Info("window", "radius de esquina del tema: " +
                                        std::to_string(radius) + "px");
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
                    log::StartupMark("nav iniciada");
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

    // protocol API: esquemas registrados por el main antes de crear la ventana.
    for (const auto& s : ProtocolRegistry::Get().All())
        webview->RegisterProtocol(s.name);

    if (!opts.backgroundColor.empty()) PSetBackgroundColor(opts.backgroundColor);

    if (pdata->overlay) {
        if (pdata->isWayland) BuildResizeEdges(this);
        BuildOverlayBar(this);
    }

    return true;
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

// ── webviews embebidas (API) ─────────────────────────────────────────────────
std::string Window::Impl::PCreateWebview(const std::string& optionsJson) {
    if (!pdata || !pdata->overlay || !pdata->viewCtx)
        return "{\"message\":\"contenedor de webviews no disponible\"}";

    auto parsed = json::Parse(optionsJson);
    const json::Value& o =
        parsed.value ? *parsed.value : json::Value(nullptr);

    std::string url, ua;
    int x = 0, y = 0, w = 320, h = 240;
    bool transparent = false;
    if (o.IsObject()) {
        if (const auto* v = o.Find("url"); v && v->IsString()) url = v->AsString();
        if (const auto* v = o.Find("x"); v && v->IsNumber()) x = (int)v->AsInt();
        if (const auto* v = o.Find("y"); v && v->IsNumber()) y = (int)v->AsInt();
        if (const auto* v = o.Find("width"); v && v->IsNumber())
            w = (int)v->AsInt();
        if (const auto* v = o.Find("height"); v && v->IsNumber())
            h = (int)v->AsInt();
        if (const auto* v = o.Find("transparent"); v && v->IsBool())
            transparent = v->AsBool();
        if (const auto* v = o.Find("userAgent"); v && v->IsString())
            ua = v->AsString();
    }

    WebKitWebView* view =
        WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(pdata->viewCtx));
    const uint32_t id = pdata->nextViewId++;
    g_object_set_data(G_OBJECT(view), "ow-view-id", GUINT_TO_POINTER(id));

    g_signal_connect(view, "load-changed", G_CALLBACK(OnViewLoadChanged), this);
    g_signal_connect(view, "load-failed", G_CALLBACK(OnViewLoadFailed), this);
    g_signal_connect(view, "notify::uri", G_CALLBACK(OnViewUriChanged), this);
    g_signal_connect(view, "notify::title", G_CALLBACK(OnViewTitleChanged), this);
    // NO focusable al crear: WebKit pide foco al cargar y robaba el foco a la
    // principal (y luego no se podía recuperar). Se habilita al hacer click.
    gtk_widget_set_can_focus(GTK_WIDGET(view), FALSE);
    g_signal_connect(view, "button-press-event", G_CALLBACK(OnViewButtonPress),
                     nullptr);

    if (transparent) {
        GdkRGBA t = {0, 0, 0, 0};
        webkit_web_view_set_background_color(view, &t);
    }
    if (!ua.empty()) {
        if (WebKitSettings* st = webkit_web_view_get_settings(view))
            webkit_settings_set_user_agent(st, ua.c_str());
    }

    // Contenedor propio por hija (GtkEventBox transparente) como overlay child:
    // así solo intercepta SU rectángulo y el resto va a la webview principal.
    GtkWidget* box = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(box), FALSE);
    gtk_widget_set_halign(box, GTK_ALIGN_START);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_margin_start(box, x);
    gtk_widget_set_margin_top(box, y);
    gtk_widget_set_size_request(box, w, h);
    gtk_overlay_add_overlay(GTK_OVERLAY(pdata->overlay), box);
    gtk_container_add(GTK_CONTAINER(box), GTK_WIDGET(view));
    gtk_widget_show_all(box);

    pdata->views[id] =
        PlatformData::EmbeddedView{box, GTK_WIDGET(view), x, y, w, h, true};

    // La hija no roba el foco: se queda en la principal.
    if (webview) gtk_widget_grab_focus(GTK_WIDGET(webview->NativeWidget()));

    if (!url.empty()) webkit_web_view_load_uri(view, url.c_str());

    json::Object r;
    r.emplace_back("id", json::Value((int64_t)id));
    return json::Value(std::move(r)).Serialize();
}

std::string Window::Impl::PWebviewCommand(uint32_t id, const std::string& op,
                                          const std::string& argsJson) {
    if (!pdata) return "{\"message\":\"sin plataforma\"}";
    auto it = pdata->views.find(id);
    if (it == pdata->views.end())
        return "{\"message\":\"webview no encontrada\"}";
    PlatformData::EmbeddedView& ev = it->second;
    WebKitWebView* view = WEBKIT_WEB_VIEW(ev.view);

    auto parsed = json::Parse(argsJson);
    const json::Value& a = parsed.value ? *parsed.value : json::Value(nullptr);
    auto argStr = [&](size_t i) -> std::string {
        if (a.IsArray() && a.AsArray().size() > i && a.AsArray()[i].IsString())
            return a.AsArray()[i].AsString();
        if (i == 0 && a.IsString()) return a.AsString();
        return {};
    };
    auto argBool = [&](size_t i, bool def) -> bool {
        if (a.IsArray() && a.AsArray().size() > i && a.AsArray()[i].IsBool())
            return a.AsArray()[i].AsBool();
        if (i == 0 && a.IsBool()) return a.AsBool();
        return def;
    };
    auto argNum = [&](size_t i, double def) -> double {
        if (a.IsArray() && a.AsArray().size() > i && a.AsArray()[i].IsNumber())
            return a.AsArray()[i].AsDouble();
        if (i == 0 && a.IsNumber()) return a.AsDouble();
        return def;
    };

    if (op == "setBounds") {
        // Acepta {x,y,width,height}, [x,y,width,height] o [{...}] (el módulo
        // envuelve los args rest en un array → normalmente llega [{...}]).
        const json::Value* src = nullptr;
        if (a.IsObject())
            src = &a;
        else if (a.IsArray() && !a.AsArray().empty() &&
                 a.AsArray()[0].IsObject())
            src = &a.AsArray()[0];

        if (src) {
            if (const auto* v = src->Find("x"); v && v->IsNumber())
                ev.x = (int)v->AsInt();
            if (const auto* v = src->Find("y"); v && v->IsNumber())
                ev.y = (int)v->AsInt();
            if (const auto* v = src->Find("width"); v && v->IsNumber())
                ev.w = (int)v->AsInt();
            if (const auto* v = src->Find("height"); v && v->IsNumber())
                ev.h = (int)v->AsInt();
        } else if (a.IsArray() && a.AsArray().size() >= 4) {
            ev.x = (int)a.AsArray()[0].AsInt();
            ev.y = (int)a.AsArray()[1].AsInt();
            ev.w = (int)a.AsArray()[2].AsInt();
            ev.h = (int)a.AsArray()[3].AsInt();
        }
        gtk_widget_set_margin_start(ev.box, ev.x);
        gtk_widget_set_margin_top(ev.box, ev.y);
        gtk_widget_set_size_request(ev.box, ev.w, ev.h);
        return "null";
    }
    if (op == "load") {
        const std::string url = argStr(0);
        if (url.empty()) return "{\"message\":\"url requerida\"}";
        webkit_web_view_load_uri(view, url.c_str());
        return "null";
    }
    if (op == "back") { webkit_web_view_go_back(view); return "null"; }
    if (op == "forward") { webkit_web_view_go_forward(view); return "null"; }
    if (op == "reload") { webkit_web_view_reload(view); return "null"; }
    if (op == "stop") { webkit_web_view_stop_loading(view); return "null"; }
    if (op == "canBack")
        return webkit_web_view_can_go_back(view) ? "true" : "false";
    if (op == "canForward")
        return webkit_web_view_can_go_forward(view) ? "true" : "false";
    if (op == "getURL") {
        const gchar* u = webkit_web_view_get_uri(view);
        return json::Value(std::string(u ? u : "")).Serialize();
    }
    if (op == "getTitle") {
        const gchar* t = webkit_web_view_get_title(view);
        return json::Value(std::string(t ? t : "")).Serialize();
    }
    if (op == "eval") {
        const std::string js = argStr(0);
        if (js.empty()) return "{\"message\":\"js requerido\"}";
        webkit_web_view_evaluate_javascript(view, js.c_str(),
                                            (gssize)js.size(), nullptr, nullptr,
                                            nullptr, nullptr, nullptr);
        return "null";
    }
    if (op == "setVisible") {
        ev.visible = argBool(0, true);
        gtk_widget_set_visible(ev.box, ev.visible);
        // Al ocultar una hija, el foco vuelve a la principal.
        if (!ev.visible && webview)
            gtk_widget_grab_focus(GTK_WIDGET(webview->NativeWidget()));
        return "null";
    }
    if (op == "setZoom") {
        webkit_web_view_set_zoom_level(view, argNum(0, 1.0));
        return "null";
    }
    if (op == "devtools") {
        if (WebKitWebInspector* insp = webkit_web_view_get_inspector(view))
            webkit_web_inspector_show(insp);
        return "null";
    }
    if (op == "findInPage") {
        const std::string text = argStr(0);
        const bool back = argBool(1, false);
        if (WebKitFindController* fc = webkit_web_view_get_find_controller(view))
            webkit_find_controller_search(
                fc, text.c_str(),
                WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE |
                    (back ? WEBKIT_FIND_OPTIONS_BACKWARDS : 0),
                G_MAXUINT);
        return "null";
    }
    if (op == "findStop") {
        if (WebKitFindController* fc = webkit_web_view_get_find_controller(view))
            webkit_find_controller_search_finish(fc);
        return "null";
    }
    if (op == "destroy") {
        gtk_widget_destroy(ev.box);
        pdata->views.erase(it);
        if (webview) gtk_widget_grab_focus(GTK_WIDGET(webview->NativeWidget()));
        return "null";
    }
    return "{\"message\":\"op desconocida: " + op + "\"}";
}

} // namespace ow
