// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Common/Messages.cpp — mensajes del WebView (decode, invoke/apply, internos).
#include "Internal.hpp"
#include "../Window_p.hpp"
#include "../../Bridge/Dispatcher.hpp"
#include "../../Session/Charter.hpp"
#include "../../Core/App.hpp"
#include "../../Control/ControlServer.hpp"
#include "../../Core/Log.hpp"
#include "ow/detail/minjson.hpp"
#include "ow/Shm.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace ow {

using namespace window_detail;

// ── mensajes del WebView ─────────────────────────────────────────────────────

void Window::Impl::HandleWebViewMessage(std::string_view text) {
    bridge::Message msg;
    if (!bridge::DecodeMessage(text, msg)) {
        log::Warn("bridge", "mensaje inválido desde JS");
        return;
    }

    if (msg.type == bridge::MsgType::Invoke) {
        // Sólo los drags se resuelven aquí: necesitan la ventana INVOCANTE
        // (no la de `msg.window`) y no forman parte del descriptor .owm.
        // El resto de `ow-window` (minimize/maximize/close/setTitle/…) cae al
        // Dispatcher, que tiene el builtin registrado (App.cpp). Interceptarlo
        // entero dejaba esas funciones inalcanzables desde el renderer.
        if (msg.module == "ow-window" &&
            (msg.method == "beginMoveDrag" || msg.method == "beginResizeDrag")) {
            HandleInternalInvoke(msg);
            return;
        }

        // Puente Node: `node.call` es ASÍNCRONO (kernel → main Node → kernel →
        // renderer). Se reenvía y NO se responde aquí; el `node.respond` del
        // main resolverá el invoke con Window::Impl::ResolveInvoke. Es la vía
        // para que el renderer use Node (extension host, libs Node-only).
        if (msg.module == "node" && msg.method == "call") {
            auto parsed = json::Parse(std::string_view(msg.json));
            std::string fn;
            std::string argsJson = "[]";
            if (parsed.value && parsed.value->IsArray() &&
                !parsed.value->AsArray().empty()) {
                const auto& a = parsed.value->AsArray();
                if (a[0].IsObject()) {
                    if (const auto* f = a[0].Find("fn"); f && f->IsString())
                        fn = f->AsString();
                    if (const auto* ar = a[0].Find("args"); ar)
                        argsJson = ar->Serialize();
                } else if (a[0].IsString()) {
                    fn = a[0].AsString();
                    if (a.size() > 1) argsJson = a[1].Serialize();
                }
            }
            if (fn.empty()) {
                std::string js =
                    "window.__ow && window.__ow._apply(" +
                    std::to_string(msg.id) + ",false," +
                    json::JsLiteral(std::string("{\"message\":\"node.call: fn requerido\"}")) +
                    ")";
                if (webview) webview->EvalJS(js);
                return;
            }
            const WindowId origin = msg.window == 0 ? id : msg.window;
            std::string charterError;
            if (!GuardRendererCall(origin, "node", "call", charterError)) {
                json::Object charterFail;
                charterFail.emplace_back("message", json::Value(charterError));
                std::string js =
                    "window.__ow && window.__ow._apply(" + std::to_string(msg.id) +
                    ",false," +
                    json::JsLiteral(json::Value(std::move(charterFail)).Serialize()) +
                    ")";
                if (webview) webview->EvalJS(js);
                return;
            }
            ControlServer::Get().ForwardNodeCall(origin, msg.id, fn, argsJson);
            return;
        }

        // Charter: the renderer path is filtered (the main process is trusted).
        const WindowId origin = msg.window == 0 ? id : msg.window;
        std::string charterError;
        if (!GuardRendererCall(origin, msg.module, msg.method, charterError)) {
            json::Object charterFail;
            charterFail.emplace_back("message", json::Value(charterError));
            std::string js = BuildApplyScript(
                msg.id, false, json::Value(std::move(charterFail)).Serialize());
            if (webview) webview->EvalJS(js);
            return;
        }

        ow_request_t req{};
        req.json = msg.json.c_str();
        req.json_len = static_cast<uint32_t>(msg.json.size());
        req.bin = msg.bin.empty() ? nullptr : msg.bin.data();
        req.bin_len = static_cast<uint32_t>(msg.bin.size());

        ow_response_t res{};
        Dispatcher::Get().Execute(origin, msg.module, msg.method, &req, &res);

        std::string resultJson;
        bool ok = res.status == 0;
        if (!ok) {
            json::Object errObj;
            errObj.emplace_back("message",
                                json::Value(res.error ? std::string(res.error) : "error"));
            resultJson = json::Value(std::move(errObj)).Serialize();
        } else {
            resultJson.assign(res.json, res.json_len);
        }
        // Apply DIRECTO (no batched): los awaits de promesas en el renderer
        // dependen de esta resolución inmediata; batchearla puede deadlockear
        // cuando el propio evaluate espera la promesa.
        std::string js = BuildApplyScript(msg.id, ok, resultJson);
        if (webview) webview->EvalJS(js);
        return;
    }

    if (msg.type == bridge::MsgType::Event) {
        // IPC dirigido: to != 0 y distinta de esta ventana → enrutar al destino
        if (msg.to != 0 && msg.to != id) {
            auto it = LiveWindows().find(msg.to);
            if (it != LiveWindows().end())
                it->second->EmitToJS(msg.name, msg.json);
            return;
        }
        FireEvent(msg.name, msg.json);
    }
}

// Resuelve un `ow.invoke` del renderer de forma ASÍNCRONA (puente Node).
void Window::Impl::ResolveInvoke(WindowId windowId, uint64_t invokeId, bool ok,
                                 std::string_view json) {
    auto it = LiveWindows().find(windowId);
    if (it == LiveWindows().end()) return;
    Window* w = it->second;
    std::string js = BuildApplyScript(invokeId, ok, std::string(json));
    if (w->impl_ && w->impl_->webview) w->impl_->webview->EvalJS(js);
}

void Window::Impl::HandleInternalInvoke(const bridge::Message& msg) {
    auto respond = [&](bool ok, std::string json) {
        EnqueueOp({OpKind::Apply, std::to_string(msg.id), ok ? "true" : "false", json});
        ScheduleFlush(this);
    };
    auto parsed = json::Parse(std::string_view(msg.json));
    json::Value args = parsed.value ? std::move(*parsed.value) : json::Value(nullptr);

    if (msg.method == "beginMoveDrag") {
        PBeginMoveDrag();
        respond(true, "null");
    } else if (msg.method == "beginResizeDrag") {
        // El bridge manda los args como ARRAY (Codec.cpp serializa el array
        // `a` tal cual), así que el borde es a[0]. `Value::Find` sólo mira
        // miembros de objeto: usarlo aquí hacía que el borde se quedara
        // siempre en el default y todos los lados redimensionaran igual.
        std::string edge = "bottom-right";
        const json::Array& a = args.AsArray();
        if (!a.empty() && a[0].IsString()) edge = a[0].AsString();
        if (!IsValidResizeEdge(edge)) {
            // no arrancamos el drag: un borde desconocido antes se traducía en
            // un resize silencioso por abajo-derecha (Linux) o en nada (Win)
            respond(false, std::string("{\"message\":\"ow-window: borde de resize "
                                       "desconocido: ") +
                               edge + "\"}");
            return;
        }
        PBeginResizeDrag(edge);
        // devuelve el borde EFECTIVO: permite verificar el parseo desde un
        // test sin depender del gestor de ventanas
        json::Object o;
        o.emplace_back("edge", json::Value(edge));
        respond(true, json::Value(std::move(o)).Serialize());
    } else {
        respond(false, std::string("{\"message\":\"ow-window: función desconocida ") +
                           msg.method + "\"}");
    }
}

} // namespace ow
