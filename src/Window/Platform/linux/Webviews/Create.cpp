// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Webviews.cpp — webviews embebidas (API, WebKitGTK).
#include "../Internal.hpp"
#include "../PlatformData.hpp"
#include "../../../Window_p.hpp"
#include "../../../../Core/App.hpp"
#include "../../../../Core/Log.hpp"
#include "../../../../Control/ControlServer.hpp"
#include "../../../../Protocol/ProtocolRegistry.hpp"
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

} // namespace ow
