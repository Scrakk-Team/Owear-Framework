// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Create/Setup.cpp — fases de creacion de ventana (GTK).
#include "Internal.hpp"
#include "../PlatformData.hpp"
#include "../../../Window_p.hpp"
#include "../../../../Core/App.hpp"
#include "../../../../Core/Log.hpp"
#include "../../../../Control/ControlServer.hpp"
#include "../../../../Protocol/ProtocolRegistry.hpp"
#include "ow/detail/minjson.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace ow {

void ApplyFramelessChrome(Window::Impl* impl, bool frameless) {
    auto* pdata = impl->pdata;
    GtkWidget* win = pdata->window;
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
}

void CreateViewContext(Window::Impl* impl) {
    auto* pdata = impl->pdata;
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
}

void ApplyWindowOptions(Window::Impl* impl) {
    auto* pdata = impl->pdata;
    GtkWidget* win = pdata->window;
    const WindowOptions& opts = impl->opts;
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

    impl->PApplyTitleBar();
}

void WireWindowSignals(Window::Impl* impl) {
    GtkWidget* win = impl->pdata->window;
    g_signal_connect(win, "delete-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer user_data) -> gboolean {
                         // F3.4: el flujo central decide (veto nativo → JS → timeout)
                         static_cast<Window::Impl*>(user_data)->BeginCloseFlow();
                         return TRUE; // siempre bloqueamos; Destroy() lo cierra
                     }),
                     impl);

    g_signal_connect(win, "destroy",
                     G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
                         auto* impl = static_cast<Window::Impl*>(user_data);
                         Window::Impl::EmitPlatformEvent(impl, "closed");
                     }),
                     impl);

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
                     impl);

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
                     impl);

    g_signal_connect(win, "focus-in-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer user_data) -> gboolean {
                         Window::Impl::EmitPlatformEvent(static_cast<Window::Impl*>(user_data), "focus");
                         return FALSE;
                     }),
                     impl);
    g_signal_connect(win, "focus-out-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer user_data) -> gboolean {
                         Window::Impl::EmitPlatformEvent(static_cast<Window::Impl*>(user_data), "blur");
                         return FALSE;
                     }),
                     impl);

    // Foco en click sobre la ventana (fallback para la webview principal).
    g_signal_connect(win, "button-press-event",
                     G_CALLBACK(OnWindowButtonPress), impl);
}

bool CreateMainWebview(Window::Impl* impl, bool frameless) {
    auto* pdata = impl->pdata;
    GtkWidget* win = pdata->window;
    auto& webview = impl->webview;
    const WindowOptions& opts = impl->opts;
    gtk_widget_show_all(win);

    // Con overlay, el webview es el child principal del GtkOverlay.
    GtkWidget* contentParent = pdata->overlay ? pdata->overlay : win;
    std::vector<std::string> wargs = opts.webviewArgs;
    for (const auto& a : internal::CommandArgs()) wargs.push_back(a);
    if (!webview->Create(contentParent, WebviewArgsWithSession(wargs, opts.session)))
        return false;
    webview->SetEventSink([impl](const std::string& name, std::string_view json) {
        Window::Impl::EmitPlatformEvent(impl, name, json);
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
            gdk_window_add_filter(nullptr, ResizeEventFilter, impl);
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
            }), impl);
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
            }), impl);
        g_object_bind_property(view, "title", win, "title", G_BINDING_DEFAULT);
        g_signal_connect(view, "notify::title",
            G_CALLBACK(+[](WebKitWebView* v, GParamSpec*, gpointer ud) {
                auto* impl = static_cast<Window::Impl*>(ud);
                const gchar* t = webkit_web_view_get_title(v);
                json::Object o;
                o.emplace_back("title", json::Value(std::string(t ? t : "")));
                Window::Impl::EmitPlatformEvent(impl, "pageTitleUpdated",
                    json::Value(std::move(o)).Serialize());
            }), impl);
    }
    return true;
}

void RegisterWindowSchemes(Window::Impl* impl) {
    auto* pdata = impl->pdata;
    auto& webview = impl->webview;
    const WindowOptions& opts = impl->opts;
    // scheme app:// → sirve archivos del directorio de assets si existe
    const char* assetsDir = std::getenv("OW_ASSETS_DIR");
    if (assetsDir && *assetsDir)
        webview->RegisterAssetScheme("app", std::filesystem::path(assetsDir));
    else
        webview->RegisterAssetScheme("app", std::filesystem::current_path() / "dist");

    // protocol API: esquemas registrados por el main antes de crear la ventana.
    for (const auto& s : ProtocolRegistry::Get().All())
        webview->RegisterProtocol(s.name);

    if (!opts.backgroundColor.empty()) impl->PSetBackgroundColor(opts.backgroundColor);

    if (pdata->overlay) {
        if (pdata->isWayland) BuildResizeEdges(impl);
        BuildOverlayBar(impl);
    }
}

} // namespace ow
