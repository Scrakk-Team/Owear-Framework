// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/linux/Backend/Rpc.cpp — ejecucion de RPC y pool de hilos.
#include "../Internal.hpp"
#include "../../../Session/Charter.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ow {
namespace webkitgtk_detail {

// ── ejecución de RPC (compartida entre el hilo de UI y el pool) ─────────────
std::string ExecRpc(WindowId wid, const std::string& mod, const std::string& fn,
                    const std::string& args) {
    if (mod.empty() || fn.empty())
        return "{\"ok\":false,\"r\":{\"message\":\"ow-rpc: ruta inválida\"}}";
    // Charter: ow-rpc:// is a renderer path, so it is filtered like the WebView
    // message path.
    std::string charterError;
    if (!GuardRendererCall(wid, mod, fn, charterError)) {
        json::Object ce;
        ce.emplace_back("message", json::Value(charterError));
        return "{\"ok\":false,\"r\":" + json::Value(std::move(ce)).Serialize() + "}";
    }
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

} // namespace webkitgtk_detail
} // namespace ow
