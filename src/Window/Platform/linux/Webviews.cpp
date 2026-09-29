// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Webviews.cpp — webviews embebidas (API, WebKitGTK).
#include "Internal.hpp"
#include "PlatformData.hpp"
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
