// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/App/win/Pump.cpp — bomba de mensajes del hilo principal (Win32).
#include "Internal.hpp"
#include "../../Log.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace ow::internal {

namespace {

constexpr UINT kWmOwPump = WM_APP + 0x4F51; // mensaje interno de drenaje
DWORD g_mainThreadId = 0;
HWND g_pumpHwnd = nullptr;

std::mutex g_pendingMu;
std::queue<std::function<void()>> g_pending;
// Un único kWmOwPump en vuelo a la vez: sin esto, una ráfaga de N callbacks
// encolaba N mensajes y el loop hacía N pasadas de GetMessage (todas vacías
// menos una). Bajo carga alta eso inundaba la cola de mensajes del hilo.
std::atomic<bool> g_pumpPosted{false};

static void EnsurePumpPosted();

static void DrainPending() {
    g_pumpPosted.store(false, std::memory_order_release);
    std::queue<std::function<void()>> batch;
    {
        std::lock_guard lock(g_pendingMu);
        batch.swap(g_pending);
    }
    while (!batch.empty()) {
        auto fn = std::move(batch.front());
        batch.pop();
        try {
            fn();
        } catch (...) {
            // nunca matar el loop por un callback
        }
    }
    // Llegaron callbacks entre el reseteo del flag y el swap (o durante el
    // drenaje): rearma el pump para no perderlos.
    {
        std::lock_guard lock(g_pendingMu);
        if (g_pending.empty()) return;
    }
    EnsurePumpPosted();
}

static void EnsurePumpPosted() {
    bool expected = false;
    if (!g_pumpPosted.compare_exchange_strong(expected, true,
                                              std::memory_order_acq_rel))
        return; // ya hay un mensaje en vuelo
    if (g_pumpHwnd && PostMessageW(g_pumpHwnd, kWmOwPump, 0, 0)) return;
    // canal viejo: PostThreadMessage exige que la cola del destino exista
    // (PlatformInit la crea con PeekMessage PM_NOREMOVE); si falla lo
    // registramos — perder este mensaje = respuesta nunca entregada.
    if (!PostThreadMessageW(g_mainThreadId, kWmOwPump, 0, 0)) {
        g_pumpPosted.store(false, std::memory_order_release);
        log::Error("app", "despacho perdido: PostThreadMessageW falló (" +
                              std::to_string(GetLastError()) + ")");
    } else if (g_pumpHwnd) {
        log::Warn("app", "despacho vía PostThreadMessage (fallback)");
    }
}

} // namespace

LRESULT CALLBACK PumpWndProc(HWND h, UINT m, WPARAM, LPARAM) {
    if (m == kWmOwPump) {
        DrainPending();
        return 0;
    }
    return DefWindowProcW(h, m, 0, 0);
}

UINT PumpMessageId() { return kWmOwPump; }
void PumpSetThreadId(DWORD id) { g_mainThreadId = id; }
DWORD PumpThreadId() { return g_mainThreadId; }
void PumpSetHwnd(HWND hwnd) { g_pumpHwnd = hwnd; }
HWND PumpHwnd() { return g_pumpHwnd; }
void PumpDrain() { DrainPending(); }

void PumpPost(std::function<void()> fn) {
    {
        std::lock_guard lock(g_pendingMu);
        g_pending.push(std::move(fn));
    }
    // Un solo mensaje de pump en vuelo: bajo ráfagas evita inundar la cola.
    EnsurePumpPosted();
}

} // namespace ow::internal
