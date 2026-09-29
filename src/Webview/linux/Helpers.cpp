// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/linux/Helpers.cpp — helpers del backend WebKitGTK (RPC pool, esquemas, permisos).
#include "Internal.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ow {
namespace webkitgtk_detail {

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
                       const char* extraHeaderName,
                       const char* extraHeaderValue) {
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

/// Como FinishBytesStatic pero COPIANDO: seguro si `data` apunta a memoria
/// temporal (p. ej. un std::string local del handler de esquema).
void FinishBytesCopy(WebKitURISchemeRequest* request, const uint8_t* data, size_t len,
                     const char* mime, const char* extraHeaderName,
                     const char* extraHeaderValue) {
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    GBytes* bytes = g_bytes_new(data, len); // copia
    WebKitURISchemeResponse* resp = webkit_uri_scheme_response_new(
        g_memory_input_stream_new_from_bytes(bytes), static_cast<gint64>(len));
    g_bytes_unref(bytes);
    webkit_uri_scheme_response_set_status(resp, 200, "OK");
    webkit_uri_scheme_response_set_content_type(resp, mime);
    if (extraHeaderName) {
        SoupMessageHeaders* headers =
            soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
        soup_message_headers_append(headers, extraHeaderName,
                                    extraHeaderValue ? extraHeaderValue : "");
        webkit_uri_scheme_response_set_http_headers(resp, headers);
    }
    webkit_uri_scheme_request_finish_with_response(request, resp);
    g_object_unref(resp);
#else
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

// ── ejecución de RPC (compartida entre el hilo de UI y el pool) ─────────────
std::string ExecRpc(WindowId wid, const std::string& mod, const std::string& fn,
                    const std::string& args) {
    if (mod.empty() || fn.empty())
        return "{\"ok\":false,\"r\":{\"message\":\"ow-rpc: ruta inválida\"}}";
    ow_request_t req{};
    req.json = args.c_str();
    req.json_len = static_cast<uint32_t>(args.size());
    ow_response_t res{};
    Dispatcher::Get().Execute(wid, mod, fn, &req, &res);
    if (res.status != 0) {
        json::Object e;
        e.emplace_back("message", json::Value(std::string(res.error ? res.error : "")));
        return "{\"ok\":false,\"r\":" + json::Value(std::move(e)).Serialize() + "}";
    }
    return "{\"ok\":true,\"r\":" + std::string(res.json, res.json_len) + "}";
}

// Módulos sin UI que pueden correr en un hilo del pool (concurrencia real).
// OFF por defecto: el handoff al pool cuesta más que el trabajo para payloads
// pequeños (medido). Activable con OW_RPC_POOL=1.
bool RpcPoolEnabled() {
    const char* v = std::getenv("OW_RPC_POOL");
    return v && std::string(v) == "1";
}
bool RpcPoolModule(const std::string& m) {
    static const std::set<std::string> s = {"fs", "path", "net", "bench", "process"};
    return s.count(m) != 0;
}


gboolean RpcJobFinish(gpointer data) {
    auto* job = static_cast<RpcJob*>(data);
    FinishBytesCopy(job->request, reinterpret_cast<const uint8_t*>(job->out.data()),
                    job->out.size(), "application/json",
                    "Access-Control-Allow-Origin", "*");
    g_object_unref(job->request);
    delete job;
    return G_SOURCE_REMOVE;
}

void RpcJobRun(gpointer data, gpointer) {
    auto* job = static_cast<RpcJob*>(data);
    job->out = ExecRpc(job->wid, job->mod, job->fn, job->args);
    g_idle_add(RpcJobFinish, job); // la respuesta se entrega en el hilo de UI
}

GThreadPool* RpcPool() {
    static GThreadPool* pool = g_thread_pool_new(RpcJobRun, nullptr, 4, FALSE, nullptr);
    return pool;
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

/// Política de aceleración de hardware (OW_GPU=auto|on|off).
/// `auto` (default) = ON_DEMAND (comportamiento histórico). `off` = NEVER:
/// el renderer DMABUF cuesta ~550 ms + ~30 MB al arrancar en entornos sin GPU
/// (headless/Xvfb); las apps sin GPU o de CI deberían usar `off`.
WebKitHardwareAccelerationPolicy GpuPolicy() {
    const char* v = std::getenv("OW_GPU");
    const std::string g = v ? v : "auto";
    if (g == "off") return WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER;
    if (g == "on") return WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS;
    return WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND;
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

} // namespace webkitgtk_detail
} // namespace ow
