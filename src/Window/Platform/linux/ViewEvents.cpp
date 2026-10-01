// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/ViewEvents.cpp — webviews hijas: helpers y eventos.
#include "Internal.hpp"
#include "../../Window_p.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstdlib>
#include <string>

namespace ow {

// ── webviews embebidas (hijas) ───────────────────────────────────────────────
// Cada una es un WebKitWebView independiente (con su proceso) dentro de un
// GtkFixed superpuesto. Se controlan por API (webview.*).

std::string ViewDataDir(const char* sub) {
    std::string base;
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        base = xdg;
    else if (const char* home = std::getenv("HOME"); home && *home)
        base = std::string(home) + "/.local/share";
    else
        base = "/tmp";
    std::string app = "owear";
    if (const char* id = std::getenv("OW_APP_ID"); id && *id)
        app = id;
    else if (const char* name = std::getenv("OW_APP_NAME"); name && *name)
        app = name;
    return base + "/owear/" + app + "/webkit/embed/" + sub;
}

uint32_t ViewIdOf(GtkWidget* view) {
    return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(view), "ow-view-id"));
}

void EmitViewEvent(Window::Impl* impl, const char* name, std::string json) {
    Window::Impl::EmitPlatformEvent(impl, name, json);
}

void OnViewLoadChanged(WebKitWebView* view, WebKitLoadEvent ev, gpointer ud) {
    auto* impl = static_cast<Window::Impl*>(ud);
    const char* state = "other";
    switch (ev) {
    case WEBKIT_LOAD_STARTED: state = "started"; break;
    case WEBKIT_LOAD_REDIRECTED: state = "redirected"; break;
    case WEBKIT_LOAD_COMMITTED: state = "committed"; break;
    case WEBKIT_LOAD_FINISHED: state = "finished"; break;
    default: break;
    }
    const gchar* uri = webkit_web_view_get_uri(view);
    json::Object o;
    o.emplace_back("id", json::Value((int64_t)ViewIdOf(GTK_WIDGET(view))));
    o.emplace_back("state", json::Value(std::string(state)));
    o.emplace_back("url", json::Value(std::string(uri ? uri : "")));
    EmitViewEvent(impl, "webview.loadChanged", json::Value(std::move(o)).Serialize());
}

void OnViewUriChanged(GObject* obj, GParamSpec*, gpointer ud) {
    auto* impl = static_cast<Window::Impl*>(ud);
    const gchar* uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(obj));
    json::Object o;
    o.emplace_back("id",
                   json::Value((int64_t)ViewIdOf(GTK_WIDGET(obj))));
    o.emplace_back("url", json::Value(std::string(uri ? uri : "")));
    EmitViewEvent(impl, "webview.urlChanged", json::Value(std::move(o)).Serialize());
}

void OnViewTitleChanged(GObject* obj, GParamSpec*, gpointer ud) {
    auto* impl = static_cast<Window::Impl*>(ud);
    const gchar* title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(obj));
    json::Object o;
    o.emplace_back("id",
                   json::Value((int64_t)ViewIdOf(GTK_WIDGET(obj))));
    o.emplace_back("title", json::Value(std::string(title ? title : "")));
    EmitViewEvent(impl, "webview.titleChanged", json::Value(std::move(o)).Serialize());
}

gboolean OnViewLoadFailed(WebKitWebView* view, WebKitLoadEvent, gchar* uri,
                          GError* err, gpointer ud) {
    auto* impl = static_cast<Window::Impl*>(ud);
    json::Object o;
    o.emplace_back("id", json::Value((int64_t)ViewIdOf(GTK_WIDGET(view))));
    o.emplace_back("url", json::Value(std::string(uri ? uri : "")));
    o.emplace_back("message",
                   json::Value(std::string(err ? err->message : "")));
    EmitViewEvent(impl, "webview.loadFailed", json::Value(std::move(o)).Serialize());
    return FALSE;
}

// Foco en click: sin esto, al pulsar la webview principal el foco se quedaba en
// la hija (GTK no lo devolvía) y el teclado seguía yendo a la hija.
// OJO: las hijas se crean con can_focus=FALSE (para no robar el foco al cargar),
// y en GTK `grab_focus` sobre un widget NO focusable no hace nada → hay que
// volver a habilitarlo aquí, si no el teclado nunca llega a la hija.
gboolean OnViewButtonPress(GtkWidget* w, GdkEventButton*, gpointer) {
    gtk_widget_set_can_focus(w, TRUE);
    gtk_widget_grab_focus(w);
    return FALSE; // deja que WebKit procese el click
}

} // namespace ow
