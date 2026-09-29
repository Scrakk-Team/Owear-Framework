// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/linux/Backend/Schemes.cpp — helpers de esquemas (MIME, respuesta binaria).
#include "../Internal.hpp"

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


} // namespace webkitgtk_detail
} // namespace ow
