// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Events.cpp — window proc + registro de clase (Win32).
#include "Internal.hpp"
#include "PlatformData.hpp"
#include "../../../Control/ControlServer.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <string>

namespace ow {

std::atomic<uint32_t> g_nextWinId{1};

// Conversiones UTF-8 <-> UTF-16 correctas (mismo enfoque que
// src/Webview/win/Webview2Backend.cpp). Las conversiones ingenuas
// byte-a-byte (std::wstring(s.begin(), s.end())) truncan/corrompen
// cualquier carácter no-ASCII.

/// Coloca la barra (popup top-level owned) en la esquina superior-derecha de la
/// ventana, en coords de pantalla, y la deja visible encima del WebView2.
LRESULT CALLBACK OwWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* pdata = reinterpret_cast<Window::Impl::PlatformData*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_COMMAND: {
        if (pdata) {
            auto it = pdata->menuCmds.find(LOWORD(wp));
            if (it != pdata->menuCmds.end()) {
                auto* impl2 = ImplFromHwnd(hwnd);
                const std::string payload =
                    "{\"id\":" + ow::json::Value(it->second.first).Serialize() +
                    ",\"role\":" + ow::json::Value(it->second.second).Serialize() +
                    ",\"windowId\":" + std::to_string(impl2 ? impl2->id : 0) + "}";
                ControlServer::Get().BroadcastEvent("menu.click", payload);
            }
        }
        break;
    }
    case WM_CLOSE: {
        // F3.4: usa el flujo central (veto nativo -> aviso JS/SDK con
        // requestId -> timeout de OW_CLOSE_TIMEOUT_MS -> destroy), igual que
        // Linux (delete-event) y macOS (NSWindowWillCloseNotification). El
        // cierre real de la ventana lo decide BeginCloseFlow (via
        // RespondJsClose o el timer), así que aquí siempre bloqueamos el
        // WM_CLOSE por defecto devolviendo 0.
        if (auto* impl = ImplFromHwnd(hwnd)) {
            impl->BeginCloseFlow();
        }
        return 0;
    }
    case WM_DESTROY: {
        if (auto* impl = ImplFromHwnd(hwnd))
            Window::Impl::EmitPlatformEvent(impl, "closed");
        return 0;
    }
    case WM_GETMINMAXINFO: {
        if (auto* impl = ImplFromHwnd(hwnd)) {
            auto* mi = reinterpret_cast<MINMAXINFO*>(lp);
            if (impl->opts.minWidth > 0 || impl->opts.minHeight > 0)
                mi->ptMinTrackSize = {static_cast<LONG>(impl->opts.minWidth),
                                      static_cast<LONG>(impl->opts.minHeight)};
            if (impl->opts.maxWidth > 0) mi->ptMaxTrackSize.x = impl->opts.maxWidth;
            if (impl->opts.maxHeight > 0) mi->ptMaxTrackSize.y = impl->opts.maxHeight;
        }
        break;
    }
    case WM_SIZING: {
        if (pdata && pdata->aspect > 0.0) {
            RECT* r = reinterpret_cast<RECT*>(lp);
            const int w = r->right - r->left;
            r->bottom = r->top + static_cast<int>(w / pdata->aspect);
        }
        break;
    }
    case WM_SIZE: {
        if (auto* impl = ImplFromHwnd(hwnd)) {
            // CRÍTICO en Windows: el controller de WebView2 NO se redimensiona
            // solo. Sin esto, tras cualquier WM_SIZE queda con bounds inválidos
            // (ventana blanca). En Linux/macOS el widget nativo se ajusta solo.
            const int cw = LOWORD(lp), ch = HIWORD(lp);
            if (cw > 0 && ch > 0 && impl->webview)
                impl->webview->Resize(0, 0, cw, ch);
            PositionCaptionBar(impl->pdata);
            json::Object o;
            o.emplace_back("width", json::Value(static_cast<int64_t>(cw)));
            o.emplace_back("height", json::Value(static_cast<int64_t>(ch)));
            Window::Impl::EmitPlatformEvent(impl, "resize",
                                    json::Value(std::move(o)).Serialize());
        }
        break;
    }
    case WM_MOVE: {
        if (auto* impl = ImplFromHwnd(hwnd)) {
            PositionCaptionBar(impl->pdata);
            json::Object o;
            o.emplace_back("x", json::Value(static_cast<int64_t>(GET_X_LPARAM(lp))));
            o.emplace_back("y", json::Value(static_cast<int64_t>(GET_Y_LPARAM(lp))));
            Window::Impl::EmitPlatformEvent(impl, "move",
                                    json::Value(std::move(o)).Serialize());
        }
        break;
    }
    case WM_SHOWWINDOW: {
        // La barra es un popup owned: seguir la visibilidad de la ventana.
        if (auto* impl = ImplFromHwnd(hwnd)) {
            if (impl->pdata && impl->pdata->captionBar) {
                if (wp)
                    PositionCaptionBar(impl->pdata);
                else
                    ShowWindow(impl->pdata->captionBar, SW_HIDE);
            }
        }
        break;
    }
    case WM_ACTIVATE: {
        if (auto* impl = ImplFromHwnd(hwnd))
            Window::Impl::EmitPlatformEvent(impl, wp != WA_INACTIVE ? "focus" : "blur");
        break;
    }
    case WM_TIMER: {
        if (wp == 1) {
            auto* impl = ImplFromHwnd(hwnd);
            if (!impl) {
                KillTimer(hwnd, 1);
                return 0;
            }
            PositionCaptionBar(impl->pdata);
            // WebView2 crea su ventana hija async: reintenta subir la barra unos
            // segundos y luego para.
            if (++impl->pdata->capTicks >= 10) KillTimer(hwnd, 1);
        }
        return 0;
    }
    case WM_NCCALCSIZE: {
        // Custom: extiende el cliente a toda la ventana salvo los bordes de
        // resize (izq/der/abajo) para no perder el resize nativo. El top solo
        // se toca maximizado. Así desaparece el caption sin DwmExtendFrame
        // (que en Win10 deja bordes blancos). Técnica Kubyshkin/Electron.
        if (!wp || !(pdata && pdata->customTitlebar)) break;
        UINT dpi = GetDpiForWindow(hwnd);
        int frame_x = GetSystemMetricsForDpi(SM_CXFRAME, dpi);
        int frame_y = GetSystemMetricsForDpi(SM_CYFRAME, dpi);
        int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        RECT* r = params->rgrc;
        r->left += frame_x + padding;
        r->right -= frame_x + padding;
        r->bottom -= frame_y + padding;
        if (IsZoomed(hwnd)) r->top += frame_y + padding;
        return 0;
    }
    case WM_NCHITTEST: {
        // deja que el sistema resuelva bordes/esquinas (resize nativo).
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        switch (hit) {
        case HTNOWHERE: case HTRIGHT: case HTLEFT: case HTTOPLEFT:
        case HTTOP: case HTTOPRIGHT: case HTBOTTOMRIGHT: case HTBOTTOM:
        case HTBOTTOMLEFT:
            return hit;
        }
        if (!(pdata && pdata->customTitlebar)) return hit;
        // El ajuste de NCCALCSIZE descoloca el área de resize superior.
        UINT dpi = GetDpiForWindow(hwnd);
        int frame_y = GetSystemMetricsForDpi(SM_CYFRAME, dpi);
        int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        if (!IsZoomed(hwnd) && pt.y > 0 && pt.y < frame_y + padding) return HTTOP;
        // El drag de la titlebar lo dispara el web ([data-ow-drag] →
        // beginMoveDrag). Aquí el resto es cliente.
        return HTCLIENT;
    }
    default:
        break;
    }

    if (pdata && pdata->origProc) return CallWindowProcW(pdata->origProc, hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RegisterClassOnce() {
    static bool done = false;
    if (done) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = OwWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kOwWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    done = true;
}

} // namespace ow
