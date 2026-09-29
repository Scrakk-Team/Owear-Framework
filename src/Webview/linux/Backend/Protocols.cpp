// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/linux/Backend/Protocols.cpp — registro de esquemas (app/ow-shm/ow-sync).
#include "../WebKitGTKBackend.hpp"

namespace ow {

void WebKitGTKBackend::RegisterKernelSchemes() {
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
                // `app://host/path` → servir `path` (el host se ignora);
                // `app://name` → servir `name`. Así los recursos RELATIVOS de
                // `app://index.html` resuelven bien (`app://index.html/x` → `x`).
                if (auto slash = rel.find('/'); slash != std::string::npos)
                    rel = rel.substr(slash + 1);

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
                FinishBytesCopy(request,
                                reinterpret_cast<const uint8_t*>(body.data()),
                                body.size(), "application/json",
                                "Access-Control-Allow-Origin", "*");
            },
            nullptr, nullptr);

        // ── ow-rpc://call/<mod>/<fn>?w=<id> ─ invoke ASÍNCRONO por fetch ───
        // El body (POST) lleva los args; la respuesta vuelve en el body. SIN
        // postMessage de request ni eval de respuesta: el motor no compila
        // código por llamada. Solo para módulos nativos (.owm); los builtins
        // con estado (node, ow-window, …) siguen por el canal postMessage.
        {
            WebKitSecurityManager* sm = webkit_web_context_get_security_manager(context_);
            if (sm) {
                webkit_security_manager_register_uri_scheme_as_secure(sm, "ow-rpc");
                webkit_security_manager_register_uri_scheme_as_cors_enabled(sm, "ow-rpc");
            }
        }
        webkit_web_context_register_uri_scheme(
            context_, "ow-rpc",
            [](WebKitURISchemeRequest* request, gpointer) {
                const std::string uri = webkit_uri_scheme_request_get_uri(request);
                std::string rest = uri, query;
                const std::string prefix = "ow-rpc://call/";
                std::string mod, fn;
                if (rest.rfind(prefix, 0) == 0) {
                    rest = rest.substr(prefix.size());
                    const auto qpos = rest.find('?');
                    if (qpos != std::string::npos) {
                        query = rest.substr(qpos + 1);
                        rest = rest.substr(0, qpos);
                    }
                    const auto slash = rest.find('/');
                    mod = slash == std::string::npos ? rest : rest.substr(0, slash);
                    fn = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
                }
                WindowId wid = 0;
                if (const auto wp = query.find("w="); wp != std::string::npos)
                    wid = static_cast<WindowId>(std::strtoul(query.c_str() + wp + 2, nullptr, 10));

                std::string args = "[]";
                if (GInputStream* bodyStream =
                        webkit_uri_scheme_request_get_http_body(request)) {
                    std::string acc;
                    guint8 buf[65536];
                    gssize n = 0;
                    while ((n = g_input_stream_read(bodyStream, buf, sizeof(buf), nullptr,
                                                    nullptr)) > 0)
                        acc.append(reinterpret_cast<const char*>(buf),
                                   static_cast<size_t>(n));
                    g_object_unref(bodyStream);
                    if (!acc.empty()) args = std::move(acc);
                }

                auto send = [request](const std::string& b) {
                    FinishBytesCopy(request,
                                    reinterpret_cast<const uint8_t*>(b.data()),
                                    b.size(), "application/json",
                                    "Access-Control-Allow-Origin", "*");
                };
                if (RpcPoolEnabled() && RpcPoolModule(mod)) {
                    auto* job = new RpcJob{};
                    job->request = request;
                    g_object_ref(request);
                    job->wid = wid;
                    job->mod = mod;
                    job->fn = fn;
                    job->args = args;
                    g_thread_pool_push(RpcPool(), job, nullptr);
                    return;
                }
                send(ExecRpc(wid, mod, fn, args));
            },
            nullptr, nullptr);

    }

} // namespace ow
