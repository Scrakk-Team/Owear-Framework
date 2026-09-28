// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/linux/WebKitGTKBackend.cpp — backend WebKitGTK 4.1.
// Todo en main thread (GTK).
//
// Schemes registrados por instancia:
//   app://    → archivos del directorio de assets (dist/)
//   ow-shm:// → regiones de memoria compartida SIN copia (F3)
//   ow-sync://→ canal síncrono invoke (XHR bloqueante, escape hatch F3)
//
#include "../IWebviewBackend.hpp"
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

namespace {

const char* MimeFromExt(const std::string& ext) {
    static const std::unordered_map<std::string, const char*> k = {
        {".html", "text/html"},   {".htm", "text/html"},  {".js", "text/javascript"},
        {".mjs", "text/javascript"}, {".css", "text/css"},{".json", "application/json"},
        {".svg", "image/svg+xml"},{".png", "image/png"},  {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},  {".gif", "image/gif"},  {".webp", "image/webp"},
        {".ico", "image/x-icon"}, {".woff", "font/woff"}, {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},     {".otf", "font/otf"},   {".map", "application/json"},
        {".txt", "text/plain"},   {".wasm", "application/wasm"},
    };
    auto it = k.find(ext);
    return it != k.end() ? it->second : "application/octet-stream";
}

/// Extrae el valor de "content-type" del JSON de headers (`{"content-type":"…"}`).
std::string ContentTypeFromHeaders(const std::string& headersJson) {
    auto parsed = ow::json::Parse(headersJson);
    if (parsed.value && parsed.value->IsObject()) {
        for (const auto& [k, v] : parsed.value->AsObject()) {
            if (k == "content-type" && v.IsString()) return v.AsString();
            if (k == "Content-Type" && v.IsString()) return v.AsString();
        }
    }
    return "text/html";
}

/// Escribe un PNG de cairo a un std::string.
cairo_status_t WritePngToStdString(void* closure, const unsigned char* data,
                                   unsigned int length) {
    static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data),
                                               length);
    return CAIRO_STATUS_SUCCESS;
}

/// Escribe cualquier salida de cairo (p. ej. PDF) a un std::string.
cairo_status_t WriteToStdString(void* closure, const unsigned char* data,
                                unsigned int length) {
    static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data),
                                               length);
    return CAIRO_STATUS_SUCCESS;
}

/// webRequest (Linux): intercepta NAVEGACIONES (decide-policy) + window.open.
gboolean OnDecidePolicy(WebKitWebView*, WebKitPolicyDecision* decision,
                        WebKitPolicyDecisionType type, gpointer) {
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
        type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION)
        return FALSE;
    WebKitNavigationAction* action =
        webkit_navigation_policy_decision_get_navigation_action(
            WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    if (!action) return FALSE;
    WebKitURIRequest* req = webkit_navigation_action_get_request(action);
    const char* url = req ? webkit_uri_request_get_uri(req) : nullptr;

    // window.open / target=_blank → la app decide (setWindowOpenHandler).
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION &&
        ow::WindowOpenBroker::Get().Enabled()) {
        g_object_ref(decision);
        ow::WindowOpenBroker::Get().Request(
            std::string(url ? url : ""), [decision](bool allow) {
                if (allow)
                    webkit_policy_decision_use(decision);
                else
                    webkit_policy_decision_ignore(decision);
                g_object_unref(decision);
            });
        return TRUE;
    }

    if (!ow::WebRequestBroker::Get().Enabled()) return FALSE;
    if (!url || !ow::WebRequestBroker::Get().Matches(url)) return FALSE;

    g_object_ref(decision);
    ow::WebRequestBroker::Get().BeforeRequest(
        std::string(url), "GET", "{}", [decision](ow::WebRequestBroker::Action a) {
            if (a.cancel) webkit_policy_decision_ignore(decision);
            else webkit_policy_decision_use(decision);
            g_object_unref(decision);
        });
    return TRUE; // gestionado (deferido)
}

/// Permisos del WebView: delega en PermissionBroker (la app decide).
gboolean OnPermissionRequest(WebKitWebView*, WebKitPermissionRequest* req, gpointer) {    const char* name = "unknown";
    if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(req)) name = "geolocation";
    else if (WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(req)) name = "notifications";
    else if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(req)) name = "media";
    else if (WEBKIT_IS_POINTER_LOCK_PERMISSION_REQUEST(req)) name = "pointerLock";
    g_object_ref(req);
    ow::PermissionBroker::Get().Request(name, "", [req](bool allow) {
        if (allow) webkit_permission_request_allow(req);
        else webkit_permission_request_deny(req);
        g_object_unref(req);
    });
    return TRUE;
}

/// Respuesta binaria sin copia: GBytes estático sobre memoria existente.
void FinishBytesStatic(WebKitURISchemeRequest* request,
                       const uint8_t* data, size_t len,
                       const char* mime,
                       const char* extraHeaderName = nullptr,
                       const char* extraHeaderValue = nullptr) {
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    GBytes* bytes = g_bytes_new_static(data, len);
    WebKitURISchemeResponse* resp = webkit_uri_scheme_response_new(
        g_memory_input_stream_new_from_bytes(bytes),
        static_cast<gint64>(len));
    g_bytes_unref(bytes);
    webkit_uri_scheme_response_set_status(resp, 200, "OK");
    webkit_uri_scheme_response_set_content_type(resp, mime);
    if (extraHeaderName) {
        SoupMessageHeaders* headers = soup_message_headers_new(
            SOUP_MESSAGE_HEADERS_RESPONSE);
        soup_message_headers_append(headers, extraHeaderName,
                                    extraHeaderValue ? extraHeaderValue : "");
        webkit_uri_scheme_response_set_http_headers(resp, headers);
    }
    webkit_uri_scheme_request_finish_with_response(request, resp);
    g_object_unref(resp);
#else
    // fallback pre-2.40: copia única al GBytes (no ocurre en 4.1 moderno)
    auto* copy = new std::string(reinterpret_cast<const char*>(data), len);
    GBytes* bytes = g_bytes_new_with_free_func(
        copy->data(), copy->size(),
        [](gpointer p) { delete static_cast<std::string*>(p); }, copy);
    GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
    g_bytes_unref(bytes);
    webkit_uri_scheme_request_finish(request, stream, -1, mime);
    g_object_unref(stream);
#endif
}

void FinishError(WebKitURISchemeRequest* request, int code, const char* msg) {
    GError* err = g_error_new(G_IO_ERROR, code, "%s", msg);
    webkit_uri_scheme_request_finish_error(request, err);
    g_error_free(err);
}

// Directorio de datos del WebView AISLADO POR APP (data/cache). Antes se usaba
// el WebsiteDataManager por defecto (compartido) → localStorage/IndexedDB/cache
// se cruzaban entre apps Owear. Se elige por OW_APP_ID (o OW_APP_NAME).
// `partition` añade aislamiento por perfil (session API).
std::string WebviewDataDir(const std::string& partition, const char* sub) {
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
    // Sanea la partición para que sea un nombre de carpeta válido.
    std::string part = partition.empty() ? "default" : partition;
    for (char& c : part)
        if (c == ':' || c == '/' || c == '\\' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            c = '_';
    return base + "/owear/" + app + "/webkit/" + part + "/" + sub;
}

/// Extrae `ow-partition=<nombre>` de los args del WebView.
std::string PartitionFromArgs(const std::vector<std::string>& args) {
    static const std::string kPrefix = "ow-partition=";
    for (const auto& a : args) {
        if (a.rfind(kPrefix, 0) == 0 && a.size() > kPrefix.size())
            return a.substr(kPrefix.size());
    }
    return {};
}

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
        WebKitWebsiteDataManager* dm = webkit_website_data_manager_new(
            "base-data-directory", dataDir.c_str(), "base-cache-directory",
            cacheDir.c_str(), nullptr);
        context_ = webkit_web_context_new_with_website_data_manager(dm);

        // Construct properties: contexto propio + NUESTRO content manager
        // (si no, WebKit crea el suyo y los scripts nunca llegan).
        view_ = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                                        "web-context", context_,
                                        "user-content-manager", manager_,
                                        nullptr));

        RegisterKernelSchemes();

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
    void RegisterKernelSchemes() {
        // app:// como esquema SEGURO + CORS: garantiza un origin estable para
        // localStorage/IndexedDB y evita restricciones de secure-context.
        if (WebKitSecurityManager* sm =
                webkit_web_context_get_security_manager(context_)) {
            webkit_security_manager_register_uri_scheme_as_secure(sm, "app");
            webkit_security_manager_register_uri_scheme_as_cors_enabled(sm, "app");
        }

        // ── app:// ─ assets del bundle ───────────────────────────────────
        webkit_web_context_register_uri_scheme(
            context_, "app",
            [](WebKitURISchemeRequest* request, gpointer user_data) {
                auto* self = static_cast<WebKitGTKBackend*>(user_data);
                const auto& root = self->assetRoot_;
                if (root.empty()) {
                    FinishError(request, G_IO_ERROR_NOT_FOUND, "assets no configurados");
                    return;
                }
                std::string uri = webkit_uri_scheme_request_get_uri(request);
                auto pos = uri.find("://");
                std::string rel =
                    pos == std::string::npos ? "" : uri.substr(pos + 3);

                std::error_code ec;
                std::filesystem::path full = (root / rel).lexically_normal();
                auto [rEnd, fEnd] =
                    std::mismatch(root.begin(), root.end(), full.begin());
                if (rEnd != root.end()) {
                    FinishError(request, G_IO_ERROR_PERMISSION_DENIED, "forbidden");
                    return;
                }
                if (!std::filesystem::is_regular_file(full, ec)) {
                    FinishError(request, G_IO_ERROR_NOT_FOUND, "not found");
                    return;
                }
                auto size = std::filesystem::file_size(full, ec);
                if (ec) {
                    FinishError(request, G_IO_ERROR_FAILED, "stat failed");
                    return;
                }
                FILE* f = std::fopen(full.c_str(), "rb");
                if (!f) {
                    FinishError(request, G_IO_ERROR_FAILED, "open failed");
                    return;
                }
                // assets pequeños: una copia aceptable; grandes → ow-shm://
                auto* payload = new std::string;
                payload->resize(size);
                size_t rd = std::fread(payload->data(), 1, size, f);
                std::fclose(f);
                payload->resize(rd);
                GBytes* bytes = g_bytes_new_with_free_func(
                    payload->data(), payload->size(),
                    [](gpointer p) { delete static_cast<std::string*>(p); },
                    payload);
                GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
                g_bytes_unref(bytes);
                webkit_uri_scheme_request_finish(
                    request, stream, -1, MimeFromExt(full.extension().string()));
                g_object_unref(stream);
            },
            this, nullptr);

        // ── ow-shm://<id> ─ regiones compartidas SIN COPIA (F3) ─────────
        webkit_web_context_register_uri_scheme(
            context_, "ow-shm",
            [](WebKitURISchemeRequest* request, gpointer) {
                std::string uri = webkit_uri_scheme_request_get_uri(request);
                auto pos = uri.find("://");
                std::string id = pos == std::string::npos ? "" : uri.substr(pos + 3);
                auto amp = id.find('?');
                if (amp != std::string::npos) id.resize(amp);

                size_t len = 0;
                const uint8_t* data = shm::Data(id.c_str(), &len);
                if (!data) {
                    FinishError(request, G_IO_ERROR_NOT_FOUND, "región inexistente");
                    return;
                }
                // cero copias del lado kernel: GBytes estático sobre el mmap
                FinishBytesStatic(request, data, len, "application/octet-stream",
                                  "Access-Control-Allow-Origin", "*");
            },
            nullptr, nullptr);

        // ── ow-sync://i/<payload-urlenc> ─ invoke SÍNCRONO (F3) ─────────
        // El renderer se bloquea en XHR hasta que este handler responde.
        // ⚠️ REENTRANCIA: el handler corre en el main thread con JS bloqueado.
        //    Las funciones invocadas aquí NO deben llamar EvalJS.
        webkit_web_context_register_uri_scheme(
            context_, "ow-sync",
            [](WebKitURISchemeRequest* request, gpointer) {
                namespace br = ow::bridge;
                std::string uri = webkit_uri_scheme_request_get_uri(request);
                auto slash = uri.rfind('/');
                std::string enc =
                    slash == std::string::npos ? "" : uri.substr(slash + 1);

                // decodifica %XX (+ deja '+' como espacio no aplica aquí:
                // encodeURIComponent no genera '+')
                std::string text;
                text.reserve(enc.size());
                for (size_t i = 0; i < enc.size(); ++i) {
                    if (enc[i] == '%' && i + 2 < enc.size()) {
                        auto hexv = [](char c) -> int {
                            if (c >= '0' && c <= '9') return c - '0';
                            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                            return -1;
                        };
                        int hi = hexv(enc[i + 1]), lo = hexv(enc[i + 2]);
                        if (hi >= 0 && lo >= 0) {
                            text += static_cast<char>((hi << 4) | lo);
                            i += 2;
                            continue;
                        }
                    }
                    text += enc[i];
                }

                br::Message msg;
                std::string body;
                if (!br::DecodeMessage(text, msg)) {
                    body = "{\"ok\":false,\"error\":\"mensaje inválido\"}";
                } else {
                    ow_request_t req{};
                    req.json = msg.json.c_str();
                    req.json_len = static_cast<uint32_t>(msg.json.size());
                    req.bin = msg.bin.empty() ? nullptr : msg.bin.data();
                    req.bin_len = static_cast<uint32_t>(msg.bin.size());
                    ow_response_t res{};
                    Dispatcher::Get().Execute(msg.window, msg.module, msg.method,
                                              &req, &res);
                    if (res.status != 0) {
                        json::Object e;
                        e.emplace_back("message", json::Value(std::string(
                                                      res.error ? res.error : "")));
                        body = "{\"ok\":false,\"r\":" +
                               json::Value(std::move(e)).Serialize() + "}";
                    } else {
                        body = "{\"ok\":true,\"r\":" +
                               std::string(res.json, res.json_len) + "}";
                    }
                }
                FinishBytesStatic(request,
                                  reinterpret_cast<const uint8_t*>(body.data()),
                                  body.size(), "application/json",
                                  "Access-Control-Allow-Origin", "*");
            },
            nullptr, nullptr);
    }

    GtkWidget* view_ = nullptr;
    WebKitUserContentManager* manager_ = nullptr;
    WebKitWebContext* context_ = nullptr;
    WebMessageHandler handler_;
    WebviewEventSink sink_;
    std::filesystem::path assetRoot_;
    std::set<std::string> registeredSchemes_;
};

} // namespace

std::unique_ptr<IWebviewBackend> CreateWebviewBackend() {
    return std::make_unique<WebKitGTKBackend>();
}

} // namespace ow
