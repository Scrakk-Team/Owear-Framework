// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/App_win.cpp — main loop Win32.
//
#include "App.hpp"
#include "Log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h> // CoInitializeEx (LEAN_AND_MEAN lo excluye)

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>


#include "App/win/Internal.hpp"

namespace ow::internal {

bool PlatformInit(int argc, char** argv) {
    (void)argc;
    (void)argv;
    PumpSetThreadId(GetCurrentThreadId());
    // DPI awareness ANTES de crear ventanas: sin esto, en monitores escalados
    // WebView2/el layout quedan mal (patrón de ole).
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InstallCrashHandlers();

    // COM apartment single-threaded para WebView2
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comOk = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    if (!comOk)
        log::Error("app", "CoInitializeEx falló: " + std::to_string(hr));

    // Fuerza la creación de la cola de mensajes del hilo principal AHORA:
    // PostThreadMessageW desde el hilo lector pierde el mensaje (devuelve
    // FALSE sin error visible en nuestro log) si la cola aún no existe.
    MSG probe;
    PeekMessageW(&probe, nullptr, 0, 0, PM_NOREMOVE);

    // ventana fantasma en el hilo principal: canal de despacho confiable.
    // Si falla, PlatformPost cae al canal viejo (PostThreadMessage).
    WNDCLASSW wc{};
    wc.lpfnWndProc = &PumpWndProc;
    wc.lpszClassName = L"owear-pump";
    wc.hInstance = GetModuleHandleW(nullptr);
    ATOM atom = RegisterClassW(&wc);
    HWND pump = nullptr;
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log::Error("app", "RegisterClassW(pump) falló: " +
                              std::to_string(GetLastError()));
    } else {
        pump = CreateWindowExW(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (!pump) {
            DWORD err = GetLastError();
            log::Warn("app", "ventana pump HWND_MESSAGE no disponible (" +
                                 std::to_string(err) + ") — reintento normal");
            // reintento como ventana oculta común (algunos entornos de
            // sesión no interactiva rechazan message-only windows)
            pump = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0,
                                   0, 0, nullptr, nullptr, wc.hInstance, nullptr);
            if (!pump) {
                log::Warn("app", "ventana pump no disponible (" +
                                     std::to_string(GetLastError()) +
                                     ") — despacho vía PostThreadMessage");
            }
        }
    }
    PumpSetHwnd(pump);
    if (PumpHwnd()) log::Info("app", "pump window lista");

    // tolerante: el kernel arranca aunque el pump window no exista
    return true;
}

void PlatformPost(std::function<void()> fn) { PumpPost(std::move(fn)); }

int RunMainLoop() {
    MSG msg;
    BOOL r;
    while ((r = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        PumpDrain();
        if (msg.message == WM_QUIT) break;
    }
    return 0;
}

void PlatformQuit() {
    // WM_QUIT vía mensaje de hilo: seguro desde cualquier hilo.
    PostThreadMessageW(PumpThreadId(), WM_QUIT, 0, 0);
}

void PlatformDelay(int ms, std::function<void()> fn) {
    auto alive = std::make_shared<std::atomic<bool>>(true);
    auto* boxed = new std::function<void()>(std::move(fn));
    std::thread([ms, boxed, alive] {
        Sleep(static_cast<DWORD>(ms));
        if (!alive->load()) {
            delete boxed;
            return;
        }
        PlatformPost([boxed] {
            std::unique_ptr<std::function<void()>> f(boxed);
            try {
                (*f)();
            } catch (...) {
            }
        });
    }).detach();
}

} // namespace ow::internal
