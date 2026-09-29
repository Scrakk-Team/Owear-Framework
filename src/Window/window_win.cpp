// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/window_win.cpp — ventana Win32 + WebView2 + titlebar.
//
// Titlebar:
//  - Default: WS_OVERLAPPEDWINDOW estándar.
//  - Hidden/Custom: WS_POPUP (frameless) + drag via WM_NCLBUTTONDOWN/HTCAPTION.
//    Overlay nativo (botones min/max/close dibujados por DWM sobre la titlebar
//    custom) usa la técnica Chromium: WM_NCCALCSIZE con frame extendido.
//    VERIFICAR-EN-WINDOWS: hit-testing de botones y snap layouts (F3).
//
#include "Window_p.hpp"
#include "Platform/win/Internal.hpp"
#include "../Core/Log.hpp"
#include "../Core/App.hpp"
#include "../Control/ControlServer.hpp"
#include "ow/detail/minjson.hpp"
#include "ow/Menu.hpp"
#include "../Protocol/ProtocolRegistry.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE // IDC_ARROW y macros de recurso expanden a la variante W
#endif
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// WebView2 (para las webviews embebidas). Si no está el SDK, se compilan a
// vacío las funciones de webviews (Linux las tiene; Windows las añade aquí).
#if __has_include(<WebView2.h>)
#include <WebView2.h>
#include <wrl/client.h>
#include <wrl/event.h>
#define OW_HAS_WEBVIEW2 1
#else
#define OW_HAS_WEBVIEW2 0
#endif

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <cstring>
#include <string>
#include <vector>

namespace ow {



namespace {


} // namespace

namespace {
std::atomic<uint32_t> g_nextWinId{1};
constexpr wchar_t kOwWindowClass[] = L"OwearWindow";

// Conversiones UTF-8 <-> UTF-16 correctas (mismo enfoque que
// src/Webview/win/Webview2Backend.cpp). Las conversiones ingenuas
// byte-a-byte (std::wstring(s.begin(), s.end())) truncan/corrompen
// cualquier carácter no-ASCII.
constexpr COLORREF kCapKey = RGB(255, 0, 255); // color transparente (colorkey)

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
} // namespace

Window::~Window() = default;
Window::Impl::~Impl() {
    alive->store(false); // callbacks diferidos dejan de tocar this
    delete pdata;
}

bool Window::Impl::PCreate() {
    log::Info("window", "PCreate: inicio");
    pdata = new PlatformData();
    RegisterClassOnce();

    DWORD style = WS_OVERLAPPEDWINDOW;
    pdata->customTitlebar = opts.titleBarStyle == TitleBarStyle::Custom &&
                            !opts.frameless;
    if (opts.frameless || opts.titleBarStyle == TitleBarStyle::Hidden)
        style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU |
                (opts.resizable ? WS_MAXIMIZEBOX | WS_MINIMIZEBOX : 0);
    // Custom: conservamos WS_OVERLAPPEDWINDOW (Electron hace lo mismo: así DWM
    // mantiene esquinas redondeadas/sombra y las animaciones min/max en Win11).
    // El caption se oculta con WM_NCCALCSIZE + SWP_FRAMECHANGED (abajo), no
    // quitando WS_CAPTION.

    {
        char sbuf[16];
        std::snprintf(sbuf, sizeof(sbuf), "0x%08lX",
                      static_cast<unsigned long>(style));
        log::Info("window", std::string("PCreate: estilo=") + sbuf +
                                " custom=" + (pdata->customTitlebar ? "1" : "0"));
    }

    std::wstring title = Utf8ToWide(opts.title);
    HWND hwnd = CreateWindowExW(0, kOwWindowClass, title.c_str(), style,
                                CW_USEDEFAULT, CW_USEDEFAULT, opts.width, opts.height,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        log::Error("window", "CreateWindowExW falló: " +
                                 std::to_string(GetLastError()));
        return false;
    }
    log::Info("window", "PCreate: hwnd creado");

    pdata->hwnd = hwnd;
    HwndMap()[hwnd] = this;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pdata));
    // NO subclasar: la clase OwearWindow ya registra OwWndProc. Un
    // SetWindowLongPtr(GWLP_WNDPROC, OwWndProc) aquí devolvería como
    // origProc el propio OwWndProc → CallWindowProcW recursivo infinito
    // → 0xC00000FD en la init de WebView2 (mordido en CI).
    pdata->origProc = nullptr;

    if (pdata->customTitlebar) {
        // CRÍTICO: reaplica el marco AHORA que pdata está puesto. El primer
        // WM_NCCALCSIZE ocurre dentro de CreateWindowEx (pdata aún null → lo
        // maneja DefWindowProc y el caption se queda). Esto lo fuerza con
        // nuestro handler → desaparece el titlebar del sistema (Win10).
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }

    // C9: opciones de estilo/estado
    {
        LONG_PTR st = GetWindowLongPtrW(hwnd, GWL_STYLE);
        st = opts.resizable ? (st | WS_THICKFRAME) : (st & ~WS_THICKFRAME);
        st = opts.maximizable ? (st | WS_MAXIMIZEBOX) : (st & ~WS_MAXIMIZEBOX);
        st = opts.minimizable ? (st | WS_MINIMIZEBOX) : (st & ~WS_MINIMIZEBOX);
        if (!opts.closable) st &= ~WS_SYSMENU;
        SetWindowLongPtrW(hwnd, GWL_STYLE, st);
        if (opts.skipTaskbar) {
            LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW;
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
        }
        if (opts.alwaysOnTop)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        pdata->aspect = opts.aspectRatio;
        if (opts.parent) {
            auto it = LiveWindows().find(opts.parent);
            if (it != LiveWindows().end()) {
                HWND owner = static_cast<HWND>(it->second->NativeHandle());
                if (owner) {
                    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT,
                                      reinterpret_cast<LONG_PTR>(owner));
                    if (opts.modal) EnableWindow(owner, FALSE);
                }
            }
        }
    }

    log::Info("window", "PCreate: webview->Create");
    std::vector<std::string> wargs = opts.webviewArgs;
    for (const auto& a : internal::CommandArgs()) wargs.push_back(a);
    if (!webview->Create(hwnd, WebviewArgsWithSession(wargs, opts.session))) {
        log::Error("window", "backend webview rechazó la creación");
        return false;
    }
    webview->SetEventSink([this](const std::string& name, std::string_view json) {
        Window::Impl::EmitPlatformEvent(this, name, json);
    });
    if (!opts.backgroundColor.empty()) PSetBackgroundColor(opts.backgroundColor);

    const char* assetsDir = std::getenv("OW_ASSETS_DIR");
    if (assetsDir && *assetsDir)
        webview->RegisterAssetScheme("app", std::filesystem::path(assetsDir));
    else
        webview->RegisterAssetScheme("app", std::filesystem::current_path() / "dist");

    // protocol API: esquemas registrados por el main antes de crear la ventana.
    for (const auto& s : ProtocolRegistry::Get().All())
        webview->RegisterProtocol(s.name);

    // titleBarOverlay: botones nativos (min/max/close) sobre la titlebar custom.
    if (pdata->customTitlebar && opts.titleBarOverlay.enabled) {
        RegisterCaptionClassOnce();
        pdata->captionH =
            opts.titleBarOverlay.height > 0 ? opts.titleBarOverlay.height : 32;
        pdata->capBg = ParseHexColorRef(opts.titleBarOverlay.color, RGB(32, 32, 32));
        pdata->capFg =
            ParseHexColorRef(opts.titleBarOverlay.symbolColor, RGB(255, 255, 255));
        pdata->captionBar = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCaptionClass,
            L"", WS_POPUP, 0, 0, 10, 10, hwnd /*owner*/, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (pdata->captionBar) {
            SetWindowLongPtrW(pdata->captionBar, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(pdata));
            PositionCaptionBar(pdata);
            webview->InjectInitScript(
                "window.__owTitlebarOverlay={enabled:true,height:" +
                std::to_string(pdata->captionH) + ",width:" +
                std::to_string(pdata->capW) + ",top:0,right:0};");
            char cbuf[64];
            std::snprintf(cbuf, sizeof(cbuf), "bg=0x%06lX fg=0x%06lX",
                          static_cast<unsigned long>(pdata->capBg),
                          static_cast<unsigned long>(pdata->capFg));
            log::Info("window", std::string("titleBarOverlay: barra creada ") +
                                    cbuf + " w=" + std::to_string(pdata->capW) +
                                    " h=" + std::to_string(pdata->capH));
            SetTimer(hwnd, 1, 400, nullptr);
        }
    }

    if (opts.show) ShowWindow(hwnd, SW_SHOW);
    log::Info("window", "PCreate: ok");
    return true;
}

void Window::Impl::PShow() { if (pdata) ShowWindow(pdata->hwnd, SW_SHOW); }
void Window::Impl::PHide() { if (pdata) ShowWindow(pdata->hwnd, SW_HIDE); }
void Window::Impl::PFocus() { if (pdata) SetForegroundWindow(pdata->hwnd); }
void Window::Impl::PClose() { if (pdata) PostMessageW(pdata->hwnd, WM_CLOSE, 0, 0); }
void Window::Impl::PDestroy() {
    if (pdata) {
        HwndMap().erase(pdata->hwnd);
        DestroyWindow(pdata->hwnd);
    }
}
void Window::Impl::PMinimize() {
    if (pdata) ShowWindow(pdata->hwnd, SW_MINIMIZE);
}
void Window::Impl::PMaximize() {
    if (pdata) ShowWindow(pdata->hwnd, SW_MAXIMIZE);
}
void Window::Impl::PUnmaximize() {
    if (pdata) ShowWindow(pdata->hwnd, SW_RESTORE);
}
void Window::Impl::PRestore() {
    if (pdata) ShowWindow(pdata->hwnd, SW_RESTORE);
}
void Window::Impl::PSetFullScreen(bool enabled) {
    if (!pdata) return;
    if (enabled) {
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(pdata->hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        pdata->preFullscreen = {sizeof(WINDOWPLACEMENT)};
        GetWindowPlacement(pdata->hwnd, &pdata->preFullscreen);
        SetWindowLongW(pdata->hwnd, GWL_STYLE,
                       GetWindowLongW(pdata->hwnd, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(pdata->hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        pdata->fullscreen = true;
    } else {
        SetWindowLongW(pdata->hwnd, GWL_STYLE,
                       GetWindowLongW(pdata->hwnd, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(pdata->hwnd, &pdata->preFullscreen);
        SetWindowPos(pdata->hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        pdata->fullscreen = false;
    }
}
bool Window::Impl::PIsMaximized() const {
    return pdata && IsZoomed(pdata->hwnd);
}
bool Window::Impl::PIsMinimized() const {
    return pdata && IsIconic(pdata->hwnd);
}
bool Window::Impl::PIsFullScreen() const { return pdata && pdata->fullscreen; }

Window::Bounds Window::Impl::PGetBounds() const {
    Bounds b;
    if (!pdata) return b;
    RECT r;
    if (GetWindowRect(pdata->hwnd, &r)) {
        b.x = r.left; b.y = r.top;
        b.w = r.right - r.left; b.h = r.bottom - r.top;
    }
    return b;
}
void Window::Impl::PSetBounds(const Bounds& bounds) {
    if (!pdata) return;
    MoveWindow(pdata->hwnd, bounds.x, bounds.y, bounds.w, bounds.h, TRUE);
}
void Window::Impl::PCenter() {
    if (!pdata) return;
    RECT rc;
    GetWindowRect(pdata->hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(pdata->hwnd, nullptr, (sw - w) / 2, (sh - h) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
}

} // namespace ow
