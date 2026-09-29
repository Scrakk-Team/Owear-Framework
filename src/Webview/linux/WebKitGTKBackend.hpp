// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/linux/WebKitGTKBackend.hpp — declaracion del backend WebKitGTK.
#pragma once
#include "../IWebviewBackend.hpp"
#include "Internal.hpp"
#include "../../Bridge/Dispatcher.hpp"
#include "../../Bridge/Shm.hpp"
#include "../../Control/ControlServer.hpp"
#include "../../Protocol/ProtocolRegistry.hpp"
#include "../../Session/PermissionBroker.hpp"
#include "../../Session/WebRequestBroker.hpp"
#include "../../Session/WindowOpenBroker.hpp"
#include "ow/Bridge/Codec.h"
#include "../../Core/Log.hpp"
#include "ow/Base64.h"
#include "ow/detail/minjson.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <cairo-pdf.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

namespace ow {

using namespace webkitgtk_detail;

class WebKitGTKBackend final : public IWebviewBackend {
public:
    ~WebKitGTKBackend() override = default;

    bool Create(void* parentNativeWindow, const std::vector<std::string>& args) override {
        // El parent puede ser la GtkWindow o un GtkOverlay (titleBarOverlay).
        GtkWidget* parent = GTK_WIDGET(parentNativeWindow);
        if (!parent) return false;

        const std::string partition = PartitionFromArgs(args);
        manager_ = webkit_user_content_manager_new();

        // Data manager POR APP (y por partición): aísla localStorage/IndexedDB/
        // cache/cookies. Sin esto WebKit usaba el manager por defecto (compartido).
        const std::string dataDir = WebviewDataDir(partition, "data");
        const std::string cacheDir = WebviewDataDir(partition, "cache");
        std::error_code ec;
        std::filesystem::create_directories(dataDir, ec);
        std::filesystem::create_directories(cacheDir, ec);
        if (std::getenv("OW_SHARED_DATA")) {
            // Prueba/diagnóstico: data manager por defecto (compartido entre apps).
            context_ = webkit_web_context_new();
        } else {
            WebKitWebsiteDataManager* dm = webkit_website_data_manager_new(
                "base-data-directory", dataDir.c_str(), "base-cache-directory",
                cacheDir.c_str(), nullptr);
            context_ = webkit_web_context_new_with_website_data_manager(dm);
        }

        // Construct properties: contexto propio + NUESTRO content manager
        // (si no, WebKit crea el suyo y los scripts nunca llegan).
        view_ = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                                        "web-context", context_,
                                        "user-content-manager", manager_,
                                        nullptr));

        // Política de aceleración de hardware (OW_GPU=auto|on|off).
        if (WebKitSettings* st = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(view_))) {
            const auto pol = GpuPolicy();
            webkit_settings_set_hardware_acceleration_policy(st, pol);
            log::Debug("webview", std::string("aceleración GPU: ") +
                                     (pol == WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER
                                          ? "off"
                                          : pol == WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS
                                                ? "on"
                                                : "on-demand"));
        }

        RegisterKernelSchemes();
        log::StartupMark("web-context+esquemas");

        // Permisos (geolocalización, notificaciones, media, pointer-lock).
        g_signal_connect(view_, "permission-request",
                         G_CALLBACK(OnPermissionRequest), nullptr);

        // webRequest: intercepta navegaciones (decide-policy).
        g_signal_connect(view_, "decide-policy", G_CALLBACK(OnDecidePolicy), nullptr);

        // app.on('child-process-gone') si el WebProcess muere.
        g_signal_connect(view_, "web-process-crashed",
                         G_CALLBACK(+[](WebKitWebView*, gpointer) {
                             ow::ControlServer::Get().BroadcastEvent(
                                 "app.event",
                                 R"({"name":"child-process-gone","payload":{"reason":"crashed"}})");
                         }),
                         nullptr);

        // before-input-event (teclado).
        g_signal_connect(view_, "key-press-event", G_CALLBACK(OnKeyEvent), this);
        g_signal_connect(view_, "key-release-event", G_CALLBACK(OnKeyEvent), this);

        gtk_container_add(GTK_CONTAINER(parent), view_);
        gtk_widget_show(view_);
        return view_ != nullptr;
    }

    void InjectInitScript(const std::string& js) override {
        if (!manager_) {
            log::Error("webview", "InjectInitScript sin manager");
            return;
        }
        log::Debug("webview", "inyectando script de " + std::to_string(js.size()) + " bytes");
        WebKitUserScript* script = webkit_user_script_new(
            js.c_str(), WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
        webkit_user_content_manager_add_script(manager_, script);
        webkit_user_script_unref(script);
    }

    void SetMessageHandler(WebMessageHandler handler) override {
        if (!manager_) return;
        handler_ = std::move(handler);
        webkit_user_content_manager_register_script_message_handler(manager_, "ow");
        g_signal_connect(manager_, "script-message-received::ow",
                         G_CALLBACK(+[](WebKitUserContentManager*,
                                        WebKitJavascriptResult* result,
                                        gpointer user_data) {
                             auto* self = static_cast<WebKitGTKBackend*>(user_data);
                             JSCValue* value =
                                 webkit_javascript_result_get_js_value(result);
                             if (jsc_value_is_string(value)) {
                                 char* str = jsc_value_to_string(value);
                                 if (self->handler_)
                                     self->handler_(std::string_view(str));
                                 g_free(str);
                             }
                         }),
                         this);
    }

    void LoadURL(const std::string& url) override {
        if (view_)
            webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view_), url.c_str());
    }

    void EvalJS(const std::string& js, EvalCallback cb) override {
        if (!view_) return;
        auto* boxed = new EvalCallback(std::move(cb));
        webkit_web_view_evaluate_javascript(
            WEBKIT_WEB_VIEW(view_), js.c_str(), static_cast<gssize>(js.size()),
            nullptr, nullptr, nullptr,
            [](GObject* obj, GAsyncResult* res, gpointer user_data) {
                std::unique_ptr<EvalCallback> cb(static_cast<EvalCallback*>(user_data));
                GError* err = nullptr;
                JSCValue* value = webkit_web_view_evaluate_javascript_finish(
                    WEBKIT_WEB_VIEW(obj), res, &err);
                std::string result = "null";
                bool ok = true;
                if (err) {
                    ok = false;
                    result = std::string("{\"owError\":") +
                             json::Value(std::string(err->message)).Serialize() + "}";
                    g_error_free(err);
                } else if (value) {
                    char* str = jsc_value_to_string(value);
                    if (jsc_value_is_string(value)) {
                        result = json::Value(std::string(str)).Serialize();
                    } else {
                        result = (str && strcmp(str, "undefined") != 0)
                                     ? str
                                     : "null"; // undefined no es JSON válido
                    }
                    g_free(str);
                }
                if (*cb) (*cb)(result, ok);
            },
            boxed);
    }

    void RegisterAssetScheme(const std::string& scheme,
                             const std::filesystem::path& root) override {
        (void)scheme; // siempre "app" — registrado desde Create()
        assetRoot_ = root;
    }

    /// Esquema gestionado por ProtocolRegistry (dir del kernel o handler en el
    /// main). Los flags privileged se leen del registro.
    void RegisterProtocol(const std::string& scheme) override {
        if (scheme.empty() || registeredSchemes_.count(scheme)) return;
        const ProtocolScheme* s = ProtocolRegistry::Get().Find(scheme);
        if (!s) return;
        registeredSchemes_.insert(scheme);

        if (s->secure || s->cors) {
            if (WebKitSecurityManager* sm =
                    webkit_web_context_get_security_manager(context_)) {
                if (s->secure) webkit_security_manager_register_uri_scheme_as_secure(sm, scheme.c_str());
                if (s->cors) webkit_security_manager_register_uri_scheme_as_cors_enabled(sm, scheme.c_str());
            }
        }

        webkit_web_context_register_uri_scheme(
            context_, scheme.c_str(),
            [](WebKitURISchemeRequest* request, gpointer) {
                const std::string uri = webkit_uri_scheme_request_get_uri(request);
                std::string name = uri;
                const auto pos = name.find("://");
                if (pos != std::string::npos) name.resize(pos);

                g_object_ref(request); // vive hasta que responda el main
                ProtocolRegistry::Get().Dispatch(
                    name, uri, "GET", "{}", "",
                    [request](ProtocolRegistry::Response r) {
                        if (!r.resolved) {
                            FinishError(request, G_IO_ERROR_NOT_FOUND,
                                        r.error.empty() ? "no encontrado" : r.error.c_str());
                        } else {
                            GBytes* bytes = g_bytes_new(r.body.data(), r.body.size());
                            GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
                            g_bytes_unref(bytes);
                            const std::string ct = ContentTypeFromHeaders(r.headersJson);
                            webkit_uri_scheme_request_finish(request, stream, -1, ct.c_str());
                            g_object_unref(stream);
                        }
                        g_object_unref(request);
                    });
            },
            this, nullptr);
    }

    void SetEventSink(WebviewEventSink sink) override { sink_ = std::move(sink); }
    void EmitEvent(const std::string& name, const std::string& json) {
        if (sink_) sink_(name, json);
    }

    /// before-input-event: teclas del WebView (no se consumen).
    static gboolean OnKeyEvent(GtkWidget*, GdkEventKey* e, gpointer ud) {
        auto* self = static_cast<WebKitGTKBackend*>(ud);
        if (!self || !e) return FALSE;
        const char* type = (e->type == GDK_KEY_RELEASE) ? "keyUp" : "keyDown";
        std::string key;
        if (e->keyval) {
            if (const char* n = gdk_keyval_name(e->keyval)) key = n;
        }
        std::string mods = "[";
        auto add = [&](bool on, const char* m) {
            if (!on) return;
            if (mods.size() > 1) mods += ",";
            mods += "\"";
            mods += m;
            mods += "\"";
        };
        add((e->state & GDK_CONTROL_MASK) != 0, "control");
        add((e->state & GDK_SHIFT_MASK) != 0, "shift");
        add((e->state & GDK_MOD1_MASK) != 0, "alt");
        add((e->state & GDK_SUPER_MASK) != 0, "meta");
        mods += "]";
        std::string json = std::string("{\"type\":\"") + type + "\",\"key\":\"" + key +
                           "\",\"modifiers\":" + mods + "}";
        self->EmitEvent("beforeInput", json);
        return FALSE; // no consumir
    }

    void CapturePage(CaptureCallback cb) override {
        if (!view_ || !cb) {
            if (cb) cb(false, {});
            return;
        }        auto* holder = new CaptureCallback(std::move(cb));
        webkit_web_view_get_snapshot(
            WEBKIT_WEB_VIEW(view_), WEBKIT_SNAPSHOT_REGION_VISIBLE,
            WEBKIT_SNAPSHOT_OPTIONS_NONE, nullptr,
            +[](GObject* obj, GAsyncResult* result, gpointer ud) {
                auto* cb = static_cast<CaptureCallback*>(ud);
                GError* err = nullptr;
                cairo_surface_t* surface = webkit_web_view_get_snapshot_finish(
                    WEBKIT_WEB_VIEW(obj), result, &err);
                if (!surface || err) {
                    if (err) g_error_free(err);
                    (*cb)(false, {});
                } else {
                    std::string png;
                    cairo_surface_write_to_png_stream(surface, &WritePngToStdString,
                                                      &png);
                    cairo_surface_destroy(surface);
                    (*cb)(true, png);
                }
                delete cb;
            },
            holder);
    }

    void PrintToPDF(PrintCallback cb) override {
        if (!view_ || !cb) {
            if (cb) cb(false, {});
            return;
        }
        // WebKitGTK no tiene API de PDF y el backend "Print to File" de GTK
        // bloquea/sin-completar (probado). Enfoque determinista: snapshot de la
        // PÁGINA COMPLETA → PDF con Cairo (imagen; funciona en cualquier sistema).
        auto* holder = new PrintCallback(std::move(cb));
        webkit_web_view_get_snapshot(
            WEBKIT_WEB_VIEW(view_), WEBKIT_SNAPSHOT_REGION_FULL_DOCUMENT,
            WEBKIT_SNAPSHOT_OPTIONS_NONE, nullptr,
            +[](GObject* obj, GAsyncResult* res, gpointer ud) {
                auto* cb = static_cast<PrintCallback*>(ud);
                GError* err = nullptr;
                cairo_surface_t* surface = webkit_web_view_get_snapshot_finish(
                    WEBKIT_WEB_VIEW(obj), res, &err);
                if (!surface || err) {
                    if (err) g_error_free(err);
                    (*cb)(false, {});
                    delete cb;
                    return;
                }
                const double w = cairo_image_surface_get_width(surface);
                const double h = cairo_image_surface_get_height(surface);
                std::string pdf;
                cairo_surface_t* pdfs =
                    cairo_pdf_surface_create_for_stream(&WriteToStdString, &pdf, w, h);
                cairo_t* cr = cairo_create(pdfs);
                cairo_set_source_surface(cr, surface, 0, 0);
                cairo_paint(cr);
                cairo_destroy(cr);
                cairo_surface_destroy(pdfs);
                cairo_surface_destroy(surface);
                (*cb)(!pdf.empty(), pdf);
                delete cb;
            },
            holder);
    }

    void SetBackgroundColor(int r, int g, int b, int a) override {
        if (!view_) return;
        GdkRGBA c;
        c.red = r / 255.0;
        c.green = g / 255.0;
        c.blue = b / 255.0;
        c.alpha = a / 255.0;
        webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(view_), &c);
    }

    void Resize(int x, int y, int w, int h) override {
        (void)x; (void)y; (void)w; (void)h; // GTK gestiona layout
    }

    void* NativeWidget() const override { return view_; }

private:
    void RegisterKernelSchemes();

    GtkWidget* view_ = nullptr;
    WebKitUserContentManager* manager_ = nullptr;
    WebKitWebContext* context_ = nullptr;
    WebMessageHandler handler_;
    WebviewEventSink sink_;
    std::filesystem::path assetRoot_;
    std::set<std::string> registeredSchemes_;
};

} // namespace ow
