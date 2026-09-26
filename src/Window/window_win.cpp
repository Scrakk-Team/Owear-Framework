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
#include <shlobj.h>
#include <uxtheme.h>
#include <vssym32.h>
#pragma comment(lib, "uxtheme.lib")

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
#include <filesystem>
#include <map>
#include <cstring>
#include <string>
#include <vector>

namespace ow {

struct Window::Impl::PlatformData {
    HWND hwnd = nullptr;
    WNDPROC origProc = nullptr;
    bool fullscreen = false;
    bool customTitlebar = false; // F3.5: overlay DWM sobre contenido full-size
    WINDOWPLACEMENT preFullscreen{};

    // ── titleBarOverlay (botones nativos min/max/close en la titlebar custom) ──
    HWND captionBar = nullptr;
    int captionH = 32;
    int capW = 0, capH = 0;
    int capHover = -1;
    int capPress = -1;

#if OW_HAS_WEBVIEW2
    // ── webviews embebidas (hijas) ──────────────────────────────────────
    struct EmbeddedView {
        Microsoft::WRL::ComPtr<ICoreWebView2Controller> ctrl;
        Microsoft::WRL::ComPtr<ICoreWebView2> view;
        HWND host = nullptr;  // HWND hijo propio de la hija (z-order/bounds)
        int x = 0, y = 0, w = 0, h = 0;
        bool visible = true;
        bool ready = false;
        bool transparent = false;
        std::string userAgent;
        std::string pendingUrl;
    };
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> viewEnv;
    bool viewEnvReady = false;
    bool viewEnvCreating = false;
    std::vector<uint32_t> pendingViewCreate;
    std::map<uint32_t, EmbeddedView> views;
    uint32_t nextViewId = 1;
#endif
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

// ── titleBarOverlay: botones nativos (min/max/close) en la titlebar custom ──
constexpr wchar_t kCaptionClass[] = L"OwearCaptionButtons";

int CaptionButtonAt(Window::Impl::PlatformData* pd, int x) {
    if (!pd || pd->capW <= 0) return -1;
    int i = (x * 3) / pd->capW;
    return (i < 0 || i > 2) ? -1 : i;
}

/// Coloca la barra en la esquina superior-derecha y la sube al tope del z-order.
void PositionCaptionBar(Window::Impl::PlatformData* pd) {
    if (!pd || !pd->captionBar || !pd->hwnd) return;
    RECT cr;
    GetClientRect(pd->hwnd, &cr);
    UINT dpi = GetDpiForWindow(pd->hwnd);
    int bw = static_cast<int>(GetSystemMetricsForDpi(SM_CXSIZE, dpi));
    if (bw <= 0) bw = 46;
    pd->capW = bw * 3;
    pd->capH = pd->captionH > 0 ? pd->captionH : 32;
    SetWindowPos(pd->captionBar, HWND_TOP, cr.right - pd->capW, 0, pd->capW,
                 pd->capH, SWP_NOACTIVATE);
}

void DrawCaptionBar(HWND hwnd, Window::Impl::PlatformData* pd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right, h = rc.bottom;
    const int bw = w / 3;
    const bool maximized = pd && pd->hwnd && IsZoomed(pd->hwnd);
    HTHEME theme = OpenThemeData(hwnd, L"WINDOW");
    for (int i = 0; i < 3; ++i) {
        RECT r{i * bw, 0, (i == 2) ? w : (i + 1) * bw, h};
        int part = (i == 0) ? WP_MINBUTTON
                            : (i == 1) ? (maximized ? WP_RESTOREBUTTON
                                                    : WP_MAXBUTTON)
                                       : WP_CLOSEBUTTON;
        int state = CBS_NORMAL;
        if (pd && pd->capPress == i) state = CBS_PUSHED;
        else if (pd && pd->capHover == i) state = CBS_HOT;
        if (theme)
            DrawThemeBackground(theme, hdc, part, state, &r, nullptr);
        else
            FillRect(hdc, &r, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
    }
    if (theme) CloseThemeData(theme);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK CaptionWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* pd = reinterpret_cast<Window::Impl::PlatformData*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_MOUSEMOVE: {
        int i = CaptionButtonAt(pd, GET_X_LPARAM(lp));
        if (pd && i != pd->capHover) {
            pd->capHover = i;
            InvalidateRect(hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (pd) { pd->capHover = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONDOWN:
        if (pd) {
            pd->capPress = CaptionButtonAt(pd, GET_X_LPARAM(lp));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        if (!pd) return 0;
        int i = CaptionButtonAt(pd, GET_X_LPARAM(lp));
        int pressed = pd->capPress;
        pd->capPress = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (pressed < 0 || pressed != i) return 0;
        if (i == 0)
            ShowWindow(pd->hwnd, SW_MINIMIZE);
        else if (i == 1)
            ShowWindow(pd->hwnd, IsZoomed(pd->hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        else if (i == 2)
            PostMessageW(pd->hwnd, WM_CLOSE, 0, 0);
        return 0;
    }
    case WM_PAINT:
        DrawCaptionBar(hwnd, pd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RegisterCaptionClassOnce() {
    static bool done = false;
    if (done) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = CaptionWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kCaptionClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    done = true;
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
    case WM_TIMER: {
        // One-shot: sube la barra de botones cuando WebView2 ya creó su ventana
        // hija (su controller es async y podría quedar por encima).
        if (wp == 1) {
            if (auto* impl = ImplFromHwnd(hwnd)) PositionCaptionBar(impl->pdata);
            KillTimer(hwnd, 1);
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

    // titleBarOverlay: botones nativos (min/max/close) sobre la titlebar custom.
    if (pdata->customTitlebar && opts.titleBarOverlay.enabled) {
        RegisterCaptionClassOnce();
        pdata->captionH =
            opts.titleBarOverlay.height > 0 ? opts.titleBarOverlay.height : 32;
        pdata->captionBar = CreateWindowExW(
            0, kCaptionClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
            10, 10, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (pdata->captionBar) {
            SetWindowLongPtrW(pdata->captionBar, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(pdata));
            PositionCaptionBar(pdata);
            webview->InjectInitScript(
                "window.__owTitlebarOverlay={enabled:true,height:" +
                std::to_string(pdata->captionH) + ",width:" +
                std::to_string(pdata->capW) + ",top:0,right:0};");
            SetTimer(hwnd, 1, 700, nullptr);
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

// ── webviews embebidas (Windows / WebView2) ─────────────────────────────────
#if OW_HAS_WEBVIEW2

using namespace Microsoft::WRL;

namespace {

std::wstring ViewUserDataDir() {
    PWSTR local = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                       &local))) {
        dir = std::wstring(local) + L"\\owear\\WebView2-views";
        CoTaskMemFree(local);
    } else {
        dir = L".\\owear-webview2-views";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void EmitViewJson(Window::Impl* impl, const char* name, uint32_t id,
                  const char* key, std::string val) {
    json::Object o;
    o.emplace_back("id", json::Value(static_cast<int64_t>(id)));
    o.emplace_back(key, json::Value(std::move(val)));
    Window::Impl::EmitPlatformEvent(impl, name,
                                    json::Value(std::move(o)).Serialize());
}

void ApplyViewBounds(Window::Impl::PlatformData* pd, uint32_t id) {
    auto it = pd->views.find(id);
    if (it == pd->views.end()) return;
    if (it->second.host) {
        SetWindowPos(it->second.host, HWND_TOP, it->second.x, it->second.y,
                     it->second.w, it->second.h, SWP_NOACTIVATE);
    }
    if (it->second.ctrl) {
        RECT rc{0, 0, it->second.w, it->second.h}; // relativo al host
        if (!it->second.host)
            rc = {it->second.x, it->second.y, it->second.x + it->second.w,
                  it->second.y + it->second.h};
        it->second.ctrl->put_Bounds(rc);
    }
}

void WireViewEvents(Window::Impl* impl, Window::Impl::PlatformData* pd,
                    uint32_t id) {
    auto it = pd->views.find(id);
    if (it == pd->views.end() || !it->second.view) return;
    ICoreWebView2* v = it->second.view.Get();

    v->add_SourceChanged(
        Callback<ICoreWebView2SourceChangedEventHandler>(
            [impl, id](ICoreWebView2* s, ICoreWebView2SourceChangedEventArgs*)
                -> HRESULT {
                LPWSTR u = nullptr;
                if (SUCCEEDED(s->get_Source(&u)) && u) {
                    EmitViewJson(impl, "webview.urlChanged", id, "url", WideToUtf8(u));
                    CoTaskMemFree(u);
                }
                return S_OK;
            }).Get(), nullptr);
    v->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [impl, id](ICoreWebView2* s, IUnknown*) -> HRESULT {
                LPWSTR t = nullptr;
                if (SUCCEEDED(s->get_DocumentTitle(&t)) && t) {
                    EmitViewJson(impl, "webview.titleChanged", id, "title",
                                 WideToUtf8(t));
                    CoTaskMemFree(t);
                }
                return S_OK;
            }).Get(), nullptr);
    v->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [impl, id](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*)
                -> HRESULT {
                EmitViewJson(impl, "webview.loadChanged", id, "state", "started");
                return S_OK;
            }).Get(), nullptr);
    v->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [impl, id](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a)
                -> HRESULT {
                BOOL ok = FALSE;
                if (a) a->get_IsSuccess(&ok);
                if (ok) {
                    EmitViewJson(impl, "webview.loadChanged", id, "state", "finished");
                } else {
                    COREWEBVIEW2_WEB_ERROR_STATUS st =
                        COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                    if (a) a->get_WebErrorStatus(&st);
                    EmitViewJson(impl, "webview.loadFailed", id, "message",
                                 "WebErrorStatus " + std::to_string((int)st));
                }
                return S_OK;
            }).Get(), nullptr);
}

void CreateViewController(Window::Impl* impl, Window::Impl::PlatformData* pd,
                          uint32_t id) {
    if (!pd->viewEnv || !pd->hwnd) return;
    auto itv = pd->views.find(id);
    HWND parent = (itv != pd->views.end() && itv->second.host)
                      ? itv->second.host
                      : pd->hwnd;
    pd->viewEnv->CreateCoreWebView2Controller(
        parent,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [impl, pd, id](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
                if (FAILED(hr) || !c) {
                    log::Error("webview", "controller de hija falló hr=" +
                        std::to_string(static_cast<unsigned long>(hr)));
                    return E_FAIL;
                }
                auto it = pd->views.find(id);
                if (it == pd->views.end()) { c->Release(); return S_OK; }
                it->second.ctrl = c;
                it->second.ctrl->get_CoreWebView2(&it->second.view);
                it->second.ready = true;
                ApplyViewBounds(pd, id);
                it->second.ctrl->put_IsVisible(it->second.visible ? TRUE : FALSE);
                if (!it->second.userAgent.empty()) {
                    ComPtr<ICoreWebView2Settings> st;
                    if (SUCCEEDED(it->second.view->get_Settings(&st)) && st) {
                        ComPtr<ICoreWebView2Settings2> st2;
                        if (SUCCEEDED(st.As(&st2)) && st2)
                            st2->put_UserAgent(
                                Utf8ToWide(it->second.userAgent).c_str());
                    }
                }
                if (it->second.transparent) {
                    ComPtr<ICoreWebView2Controller2> c2;
                    if (SUCCEEDED(it->second.ctrl.As(&c2))) {
                        COREWEBVIEW2_COLOR col{};
                        col.A = 0; col.R = 0; col.G = 0; col.B = 0;
                        c2->put_DefaultBackgroundColor(col);
                    }
                }
                WireViewEvents(impl, pd, id);
                if (!it->second.pendingUrl.empty()) {
                    std::string u = it->second.pendingUrl;
                    it->second.pendingUrl.clear();
                    it->second.view->Navigate(Utf8ToWide(u).c_str());
                }
                return S_OK;
            }).Get());
}

void EnsureViewEnv(Window::Impl* impl, Window::Impl::PlatformData* pd) {
    if (pd->viewEnv || pd->viewEnvCreating) return;
    pd->viewEnvCreating = true;
    std::wstring dir = ViewUserDataDir();
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, dir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [impl, pd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) {
                    log::Error("webview", "environment de hijas falló");
                    return E_FAIL;
                }
                pd->viewEnv = env;
                pd->viewEnvReady = true;
                auto pending = pd->pendingViewCreate;
                pd->pendingViewCreate.clear();
                for (uint32_t id : pending) CreateViewController(impl, pd, id);
                return S_OK;
            }).Get());
    if (FAILED(hr))
        log::Error("webview",
                   "CreateCoreWebView2EnvironmentWithOptions (hijas) falló");
}

} // namespace

#endif // OW_HAS_WEBVIEW2

std::string Window::Impl::PCreateWebview(const std::string& optionsJson) {
#if OW_HAS_WEBVIEW2
    if (!pdata || !pdata->hwnd) return "{\"message\":\"sin ventana\"}";

    auto parsed = json::Parse(optionsJson);
    const json::Value& o = parsed.value ? *parsed.value : json::Value(nullptr);
    std::string url, ua;
    int x = 0, y = 0, w = 320, h = 240;
    bool transparent = false;
    if (o.IsObject()) {
        if (const auto* v = o.Find("url"); v && v->IsString()) url = v->AsString();
        if (const auto* v = o.Find("x"); v && v->IsNumber()) x = (int)v->AsInt();
        if (const auto* v = o.Find("y"); v && v->IsNumber()) y = (int)v->AsInt();
        if (const auto* v = o.Find("width"); v && v->IsNumber())
            w = (int)v->AsInt();
        if (const auto* v = o.Find("height"); v && v->IsNumber())
            h = (int)v->AsInt();
        if (const auto* v = o.Find("transparent"); v && v->IsBool())
            transparent = v->AsBool();
        if (const auto* v = o.Find("userAgent"); v && v->IsString())
            ua = v->AsString();
    }

    const uint32_t id = pdata->nextViewId++;
    PlatformData::EmbeddedView ev;
    ev.x = x; ev.y = y; ev.w = w; ev.h = h;
    ev.transparent = transparent; ev.userAgent = ua; ev.pendingUrl = url;
    pdata->views[id] = std::move(ev);

    // HWND hijo propio: el controller se parenta aquí (z-order y bounds fiables
    // por encima del webview principal, sin depender del orden interno de WebView2).
    HWND host = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, x, y, w, h,
        pdata->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (host) {
        pdata->views[id].host = host;
        SetWindowPos(host, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
    }

    if (pdata->viewEnvReady)
        CreateViewController(this, pdata, id);
    else {
        pdata->pendingViewCreate.push_back(id);
        EnsureViewEnv(this, pdata);
    }

    json::Object r;
    r.emplace_back("id", json::Value(static_cast<int64_t>(id)));
    return json::Value(std::move(r)).Serialize();
#else
    (void)optionsJson;
    return {};
#endif
}

std::string Window::Impl::PWebviewCommand(uint32_t id, const std::string& op,
                                          const std::string& argsJson) {
#if OW_HAS_WEBVIEW2
    if (!pdata) return "{}";
    auto it = pdata->views.find(id);
    if (it == pdata->views.end())
        return "{\"message\":\"webview no encontrada\"}";
    PlatformData::EmbeddedView& ev = it->second;

    auto parsed = json::Parse(argsJson);
    const json::Value& a = parsed.value ? *parsed.value : json::Value(nullptr);
    auto argStr = [&](size_t i) -> std::string {
        if (a.IsArray() && a.AsArray().size() > i && a.AsArray()[i].IsString())
            return a.AsArray()[i].AsString();
        if (i == 0 && a.IsString()) return a.AsString();
        return {};
    };
    auto argBool = [&](size_t i, bool def) -> bool {
        if (a.IsArray() && a.AsArray().size() > i && a.AsArray()[i].IsBool())
            return a.AsArray()[i].AsBool();
        if (i == 0 && a.IsBool()) return a.AsBool();
        return def;
    };
    auto argNum = [&](size_t i, double def) -> double {
        if (a.IsArray() && a.AsArray().size() > i && a.AsArray()[i].IsNumber())
            return a.AsArray()[i].AsDouble();
        if (i == 0 && a.IsNumber()) return a.AsDouble();
        return def;
    };

    if (op == "setBounds") {
        const json::Value* src = nullptr;
        if (a.IsObject())
            src = &a;
        else if (a.IsArray() && !a.AsArray().empty() && a.AsArray()[0].IsObject())
            src = &a.AsArray()[0];
        if (src) {
            if (const auto* v = src->Find("x"); v && v->IsNumber()) ev.x = (int)v->AsInt();
            if (const auto* v = src->Find("y"); v && v->IsNumber()) ev.y = (int)v->AsInt();
            if (const auto* v = src->Find("width"); v && v->IsNumber())
                ev.w = (int)v->AsInt();
            if (const auto* v = src->Find("height"); v && v->IsNumber())
                ev.h = (int)v->AsInt();
        } else if (a.IsArray() && a.AsArray().size() >= 4) {
            ev.x = (int)a.AsArray()[0].AsInt();
            ev.y = (int)a.AsArray()[1].AsInt();
            ev.w = (int)a.AsArray()[2].AsInt();
            ev.h = (int)a.AsArray()[3].AsInt();
        }
        ApplyViewBounds(pdata, id);
        return "null";
    }
    if (op == "load") {
        std::string url = argStr(0);
        if (url.empty()) return "{\"message\":\"url requerida\"}";
        if (!ev.view) { ev.pendingUrl = url; return "null"; }
        ev.view->Navigate(Utf8ToWide(url).c_str());
        return "null";
    }
    if (op == "back") { if (ev.view) ev.view->GoBack(); return "null"; }
    if (op == "forward") { if (ev.view) ev.view->GoForward(); return "null"; }
    if (op == "reload") { if (ev.view) ev.view->Reload(); return "null"; }
    if (op == "stop") { if (ev.view) ev.view->Stop(); return "null"; }
    if (op == "canBack") {
        BOOL b = FALSE; if (ev.view) ev.view->get_CanGoBack(&b);
        return b ? "true" : "false";
    }
    if (op == "canForward") {
        BOOL b = FALSE; if (ev.view) ev.view->get_CanGoForward(&b);
        return b ? "true" : "false";
    }
    if (op == "getURL") {
        LPWSTR u = nullptr; std::string s;
        if (ev.view && SUCCEEDED(ev.view->get_Source(&u)) && u) {
            s = WideToUtf8(u); CoTaskMemFree(u);
        }
        return json::Value(s).Serialize();
    }
    if (op == "getTitle") {
        LPWSTR t = nullptr; std::string s;
        if (ev.view && SUCCEEDED(ev.view->get_DocumentTitle(&t)) && t) {
            s = WideToUtf8(t); CoTaskMemFree(t);
        }
        return json::Value(s).Serialize();
    }
    if (op == "eval") {
        std::string js = argStr(0);
        if (js.empty()) return "{\"message\":\"js requerido\"}";
        if (ev.view) ev.view->ExecuteScript(Utf8ToWide(js).c_str(), nullptr);
        return "null";
    }
    if (op == "setVisible") {
        ev.visible = argBool(0, true);
        if (ev.ctrl) ev.ctrl->put_IsVisible(ev.visible ? TRUE : FALSE);
        if (ev.host) {
            if (ev.visible)
                SetWindowPos(ev.host, HWND_TOP, ev.x, ev.y, ev.w, ev.h,
                             SWP_NOACTIVATE | SWP_SHOWWINDOW);
            else
                ShowWindow(ev.host, SW_HIDE);
        }
        return "null";
    }
    if (op == "setZoom") {
        if (ev.ctrl) ev.ctrl->put_ZoomFactor(argNum(0, 1.0));
        return "null";
    }
    if (op == "devtools") { if (ev.view) ev.view->OpenDevToolsWindow(); return "null"; }
    if (op == "findInPage") {
        std::string text = argStr(0);
        bool back = argBool(1, false);
        if (ev.view) {
            std::string js = "window.find(" + json::Value(text).Serialize() +
                             (back ? ",false,true" : ",false,false") + ")";
            ev.view->ExecuteScript(Utf8ToWide(js).c_str(), nullptr);
        }
        return "null";
    }
    if (op == "findStop") return "null";
    if (op == "destroy") {
        if (ev.ctrl) ev.ctrl->Close();
        if (ev.host) DestroyWindow(ev.host);
        pdata->views.erase(it);
        return "null";
    }
    return "{\"message\":\"op desconocida: " + op + "\"}";
#else
    (void)id; (void)op; (void)argsJson;
    return {};
#endif
}

} // namespace ow
