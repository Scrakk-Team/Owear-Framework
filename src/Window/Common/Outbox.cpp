// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Common/Outbox.cpp — eventos, outbox (F3.2) y cierre con veto (F3.4).
#include "Internal.hpp"
#include "../Window_p.hpp"
#include "../../Bridge/Dispatcher.hpp"
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

// ── eventos ──────────────────────────────────────────────────────────────────

void Window::Impl::FireEvent(const std::string& name, std::string_view payloadJson) {
    auto it = listeners.find(name);
    if (it == listeners.end()) return;
    for (auto& l : it->second) l.fn(payloadJson);
}

ListenerId Window::On(const std::string& name, std::function<void(EventPayload)> cb) {
    auto& ls = impl_->listeners[name];
    ListenerId id = impl_->nextListenerId++;
    ls.push_back({id, std::move(cb)});
    return id;
}

void Window::Off(ListenerId id) {
    for (auto& [name, vec] : impl_->listeners)
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [id](const auto& l) { return l.id == id; }),
                  vec.end());
}

void Window::EmitToJS(const std::string& name, std::string_view jsonPayload) {
    impl_->EnqueueOp({Impl::OpKind::Event, std::to_string(impl_->id),
                      std::string(name), std::string(jsonPayload)});
    Impl::ScheduleFlush(impl_.get());
}

// ── F3.2 outbox ──────────────────────────────────────────────────────────────

void Window::Impl::EmitPlatformEvent(Impl* impl, const std::string& name,
                                     std::string_view payloadJson) {
    // listeners nativos inmediatos (sin batch: son C++ locales)
    impl->FireEvent(name, payloadJson);
    // JS vía outbox
    impl->EnqueueOp({Impl::OpKind::Event, std::to_string(impl->id),
                     name, std::string(payloadJson)});
    Impl::ScheduleFlush(impl);
}

void Window::Impl::EnqueueOp(Op op) {
    std::lock_guard lock(outboxMu);
    outbox.push_back(std::move(op));
}

void Window::Impl::ScheduleFlush(Impl* impl) {
    {
        std::lock_guard lock(impl->outboxMu);
        if (impl->outboxScheduled) return;
        impl->outboxScheduled = true;
    }
    auto aliveWeak = std::weak_ptr<std::atomic<bool>>(impl->alive);
    App::Post([aliveWeak, impl] {
        if (auto a = aliveWeak.lock(); a && a->load())
            FlushOutbox(impl);
    });
}

void Window::Impl::FlushOutbox(Impl* impl) {
    if (!impl->webview) return;
    std::vector<Op> batch;
    {
        std::lock_guard lock(impl->outboxMu);
        impl->outboxScheduled = false;
        batch.swap(impl->outbox);
    }
    if (batch.empty()) return;

    // un eval con __ow._batch([[kind,...],...])
    std::string script = "window.__ow && window.__ow._batch([";
    for (size_t i = 0; i < batch.size(); ++i) {
        const Op& op = batch[i];
        if (i) script += ',';
        if (op.kind == OpKind::Apply) {
            script += "[\"a\"," + op.a + ',' + op.b + ',' +
                      json::JsLiteral(op.c) + ']';
        } else {
            script += "[\"e\"," + op.a + ',' + json::JsLiteral(op.b) + ',' +
                      json::JsLiteral(op.c) + ']';
        }
    }
    script += "])";
    impl->webview->EvalJS(script);
}

// ── F3.4 flujo de cierre con veto JS ─────────────────────────────────────────

bool Window::Impl::BeginCloseFlow() {
    // ya hay una petición en vuelo → el timeout decidirá
    if (jsCloseRequestId != 0) return false;

    jsCloseRequestId = ++jsCloseSeq;
    jsCloseResponded = false;

    // payload único con requestId: nativo (veto) + JS + SDK
    json::Object payload;
    payload.emplace_back("requestId",
                         json::Value(static_cast<int64_t>(jsCloseRequestId)));
    std::string pj = json::Value(std::move(payload)).Serialize();

    FireEvent("closeRequested", pj); // nativos pueden marcar closeRequestVeto
    if (closeRequestVeto) {
        jsCloseRequestId = 0;
        return false;
    }
    EnqueueOp({OpKind::Event, std::to_string(id), "closeRequested", pj});
    ScheduleFlush(this);

    // timer de seguridad: sin respuesta → cerrar igualmente
    auto aliveWeak = std::weak_ptr<std::atomic<bool>>(alive);
    internal::PlatformDelay(CloseVetoTimeoutMs(), [this, aliveWeak] {
        if (auto a = aliveWeak.lock(); a && a->load()) CloseTimerFired(this);
    });
    return false; // el cierre real lo decide RespondJsClose o el timer
}

void Window::Impl::CloseTimerFired(Window::Impl* impl) {
    if (!impl->jsCloseResponded && impl->jsCloseRequestId != 0) {
        log::Debug("window", "close sin respuesta JS → cerrando (timeout)");
        impl->self->Destroy();
    }
}

void Window::Impl::RespondJsClose(uint64_t requestId, bool allow) {
    if (requestId != jsCloseRequestId || jsCloseResponded) return;
    jsCloseResponded = true;
    jsCloseRequestId = 0;
    if (allow) self->Destroy();
    // allow=false → cancelado; la ventana sigue viva
}

} // namespace ow
