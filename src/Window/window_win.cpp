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
#include "../Core/Log.hpp"
#include "ow/detail/minjson.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE // IDC_ARROW y macros de recurso expanden a la variante W
#endif
#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <cstdio>
#include <map>
#include <cstring>

namespace ow {

struct Window::Impl::PlatformData {
    HWND hwnd = nullptr;
    WNDPROC origProc = nullptr;
    bool fullscreen = false;
    bool customTitlebar = false; // F3.5: overlay DWM sobre contenido full-size
    WINDOWPLACEMENT preFullscreen{};
};

namespace {
std::atomic<uint32_t> g_nextWinId{1};
constexpr wchar_t kOwWindowClass[] = L"OwearWindow";

// Conversiones UTF-8 <-> UTF-16 correctas (mismo enfoque que
// src/Webview/win/Webview2Backend.cpp). Las conversiones ingenuas
// byte-a-byte (std::wstring(s.begin(), s.end())) truncan/corrompen
// cualquier carácter no-ASCII.
std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(const wchar_t* w) {
    if (!w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// mapa hwnd → Impl (los callbacks C no pueden capturar)
std::map<HWND, Window::Impl*>& HwndMap() {
    static std::map<HWND, Window::Impl*> m;
    return m;
}
Window::Impl* ImplFromHwnd(HWND hwnd) {
    auto& m = HwndMap();
    auto it = m.find(hwnd);
    return it == m.end() ? nullptr : it->second;
}

LRESULT CALLBACK OwWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* pdata = reinterpret_cast<Window::Impl::PlatformData*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
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
    case WM_SIZE: {
        if (auto* impl = ImplFromHwnd(hwnd)) {
            // CRÍTICO en Windows: el controller de WebView2 NO se redimensiona
            // solo. Sin esto, tras cualquier WM_SIZE queda con bounds inválidos
            // (ventana blanca). En Linux/macOS el widget nativo se ajusta solo.
            const int cw = LOWORD(lp), ch = HIWORD(lp);
            if (cw > 0 && ch > 0 && impl->webview)
                impl->webview->Resize(0, 0, cw, ch);
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
            json::Object o;
            o.emplace_back("x", json::Value(static_cast<int64_t>(GET_X_LPARAM(lp))));
            o.emplace_back("y", json::Value(static_cast<int64_t>(GET_Y_LPARAM(lp))));
            Window::Impl::EmitPlatformEvent(impl, "move",
                                    json::Value(std::move(o)).Serialize());
        }
        break;
    }
    case WM_ACTIVATE: {
        if (auto* impl = ImplFromHwnd(hwnd))
            Window::Impl::EmitPlatformEvent(impl, wp != WA_INACTIVE ? "focus" : "blur");
        break;
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

    if (!opts.resizable) {
        DWORD s = GetWindowLongW(hwnd, GWL_STYLE);
        SetWindowLongW(hwnd, GWL_STYLE, s & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
    }

    log::Info("window", "PCreate: webview->Create");
    if (!webview->Create(hwnd, opts.webviewArgs)) {
        log::Error("window", "backend webview rechazó la creación");
        return false;
    }

    const char* assetsDir = std::getenv("OW_ASSETS_DIR");
    if (assetsDir && *assetsDir)
        webview->RegisterAssetScheme("app", std::filesystem::path(assetsDir));
    else
        webview->RegisterAssetScheme("app", std::filesystem::current_path() / "dist");

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

void Window::Impl::PSetTitle(const std::string& t) {
    if (!pdata) return;
    std::wstring w = Utf8ToWide(t);
    SetWindowTextW(pdata->hwnd, w.c_str());
}
std::string Window::Impl::PGetTitle() const {
    if (!pdata) return {};
    wchar_t buf[512]{};
    GetWindowTextW(pdata->hwnd, buf, 512);
    return WideToUtf8(buf);
}

void Window::Impl::PApplyTitleBar() {
    // Hidden/Custom ya son frameless desde PCreate; cambios en caliente (F3)
}

void Window::Impl::PBeginMoveDrag() {
    if (!pdata) return;
    ReleaseCapture();
    PostMessageW(pdata->hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void Window::Impl::PBeginResizeDrag(const std::string& edge) {
    if (!pdata) return;
    static const std::map<std::string, UINT> kEdges = {
        {"left", HTLEFT}, {"right", HTRIGHT},
        {"top", HTTOP}, {"bottom", HTBOTTOM},
        {"top-left", HTTOPLEFT}, {"top-right", HTTOPRIGHT},
        {"bottom-left", HTBOTTOMLEFT}, {"bottom-right", HTBOTTOMRIGHT},
    };
    auto it = kEdges.find(edge);
    if (it == kEdges.end()) return;
    ReleaseCapture();
    PostMessageW(pdata->hwnd, WM_NCLBUTTONDOWN, it->second, 0);
}

// Webviews embebidas: pendiente en Windows (Linux primero).
std::string Window::Impl::PCreateWebview(const std::string&) { return {}; }
std::string Window::Impl::PWebviewCommand(uint32_t, const std::string&,
                                          const std::string&) {
    return {};
}

} // namespace ow
