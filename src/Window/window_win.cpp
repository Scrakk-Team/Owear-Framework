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

struct Window::Impl::PlatformData {
    HWND hwnd = nullptr;
    WNDPROC origProc = nullptr;
    bool fullscreen = false;
    bool kiosk = false;
    double aspect = 0.0;
    bool customTitlebar = false; // F3.5: overlay DWM sobre contenido full-size
    WINDOWPLACEMENT preFullscreen{};

    // ── titleBarOverlay (botones nativos min/max/close en la titlebar custom) ──
    HWND captionBar = nullptr;
    int captionH = 32;
    int capW = 0, capH = 0;
    int capHover = -1;
    int capPress = -1;
    int capTicks = 0;
    COLORREF capBg = RGB(32, 32, 32);
    COLORREF capFg = RGB(255, 255, 255);

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

    // ── menubar de aplicación (C5) ────────────────────────────────────────
    HMENU appMenu = nullptr;
    std::map<UINT, std::pair<std::string, std::string>> menuCmds; // cmd → {id, role}
};

namespace {

std::wstring ToWideMenu(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void BuildMenuBar(HMENU bar, const std::vector<ow::menu::Item>& items,
                  std::map<UINT, std::pair<std::string, std::string>>& cmds,
                  UINT& nextCmd) {
    for (const auto& it : items) {
        if (!it.visible) continue;
        if (it.type == "separator") {
            AppendMenuW(bar, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        const std::wstring label =
            ToWideMenu(it.label + (it.accelerator.empty() ? "" : ("\t" + it.accelerator)));
        if (!it.submenu.empty()) {
            HMENU sub = CreatePopupMenu();
            BuildMenuBar(sub, it.submenu, cmds, nextCmd);
            AppendMenuW(bar, MF_POPUP | (it.enabled ? 0 : MF_GRAYED),
                        reinterpret_cast<UINT_PTR>(sub), label.c_str());
        } else {
            UINT flags = MF_STRING;
            if (!it.enabled) flags |= MF_GRAYED;
            if (it.checked) flags |= MF_CHECKED;
            const UINT cmd = nextCmd++;
            cmds[cmd] = {it.id, it.role};
            AppendMenuW(bar, flags, cmd, label.c_str());
            if (it.type == "radio") {
                MENUITEMINFOW mii{};
                mii.cbSize = sizeof(mii);
                mii.fMask = MIIM_FTYPE;
                mii.fType = MFT_RADIOCHECK;
                SetMenuItemInfoW(bar, cmd, FALSE, &mii);
            }
        }
    }
}

} // namespace

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
constexpr COLORREF kCapKey = RGB(255, 0, 255); // color transparente (colorkey)

int CaptionButtonAt(Window::Impl::PlatformData* pd, int x) {
    if (!pd || pd->capW <= 0) return -1;
    int i = (x * 3) / pd->capW;
    return (i < 0 || i > 2) ? -1 : i;
}

/// Coloca la barra (popup top-level owned) en la esquina superior-derecha de la
/// ventana, en coords de pantalla, y la deja visible encima del WebView2.
void PositionCaptionBar(Window::Impl::PlatformData* pd) {
    if (!pd || !pd->captionBar || !pd->hwnd) return;
    if (IsIconic(pd->hwnd)) {
        ShowWindow(pd->captionBar, SW_HIDE);
        return;
    }
    RECT cr;
    GetClientRect(pd->hwnd, &cr);
    UINT dpi = GetDpiForWindow(pd->hwnd);
    int bw = MulDiv(45, static_cast<int>(dpi), 96); // kWindowsCaptionButtonWidth
    if (bw <= 0) bw = 45;
    pd->capW = bw * 3;
    pd->capH = pd->captionH > 0 ? pd->captionH : 32;
    POINT pt{cr.right - pd->capW, 0};
    ClientToScreen(pd->hwnd, &pt);
    SetWindowPos(pd->captionBar, HWND_TOP, pt.x, pt.y, pd->capW, pd->capH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(pd->captionBar, nullptr, FALSE);
}

COLORREF BlendColor(COLORREF a, COLORREF b, double t) {
    int r = static_cast<int>(GetRValue(a) * (1 - t) + GetRValue(b) * t);
    int g = static_cast<int>(GetGValue(a) * (1 - t) + GetGValue(b) * t);
    int bl = static_cast<int>(GetBValue(a) * (1 - t) + GetBValue(b) * t);
    return RGB(r, g, bl);
}

COLORREF ParseHexColorRef(const std::string& in, COLORREF def) {
    std::string s = in;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.size() < 6) return def;
    auto hx = [](const std::string& x) {
        return static_cast<unsigned>(std::strtoul(x.c_str(), nullptr, 16));
    };
    return RGB(hx(s.substr(0, 2)), hx(s.substr(2, 2)), hx(s.substr(4, 2)));
}

/// Barra de botones (ventana hija OPACA) estilo Win10/11, como Electron:
/// minimizar = línea, maximizar = cuadrado, restaurar = dos cuadrados, cerrar =
/// X; icono 10px; hover blanco 10% (close #E81123). Fondo = titleBarOverlay.color.
void EnsureGdiplus() {
    static ULONG_PTR token = 0;
    if (!token) {
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&token, &in, nullptr);
    }
}

/// Barra de botones (popup top-level LAYERED) dibujada con GDI+ (constantes de
/// Electron: icono 10px, min/max/restore sin AA y rect insetado 0.5, restore =
/// dos cuadrados de 8px desplazados 2, cerrar = X con AA recortada).
///
/// CLAVE del hit-test: en un layered con UpdateLayeredWindow Windows pasa el
/// mouse por los píxeles con alpha == 0. Para que el hover/press funcionen en
/// TODO el rect (y no solo sobre el glifo), el fondo se pinta con alpha = 1
/// (visualmente imperceptible) en vez de 0.
void DrawCaptionBar(HWND hwnd, Window::Impl::PlatformData* pd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0 || !pd) return;
    EnsureGdiplus();

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);

    UINT dpi = pd->hwnd ? GetDpiForWindow(pd->hwnd) : 96;
    if (dpi < 96) dpi = 96;
    const bool maximized = IsZoomed(pd->hwnd);
    const int bw = w / 3;
    int icon = MulDiv(10, static_cast<int>(dpi), 96);
    if (icon < 6) icon = 10;
    float stroke = static_cast<float>(MulDiv(1, static_cast<int>(dpi), 96));
    if (stroke < 1.0f) stroke = 1.0f;
    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(1, 0, 0, 0)); // alpha 1 = transparente pero hittable

        for (int i = 0; i < 3; ++i) {
            const int x0 = i * bw, x1 = (i == 2) ? w : (i + 1) * bw;
            const bool hot = pd->capHover == i;
            const bool press = pd->capPress == i;
            if (hot || press) {
                BYTE a;
                Gdiplus::Color base;
                if (i == 2) {
                    base = Gdiplus::Color(255, 0xE8, 0x11, 0x23);
                    a = press ? 0x98 : 255;
                } else {
                    base = Gdiplus::Color(255, 255, 255, 255);
                    a = press ? 0x33 : 0x1A;
                }
                Gdiplus::SolidBrush br(
                    Gdiplus::Color(a, base.GetR(), base.GetG(), base.GetB()));
                g.FillRectangle(&br, Gdiplus::Rect(x0, 0, x1 - x0, h));
            }
            Gdiplus::Pen pen(Gdiplus::Color(255, 255, 255, 255), stroke);
            // Centrado en ENTEROS (Electron: ClampToCenteredSize). Con coords
            // float a veces caía en medio píxel y el AA de la X la difuminaba.
            const int iw = x1 - x0;
            const float S = static_cast<float>(icon);
            const float sx = static_cast<float>(x0 + (iw - icon) / 2);
            const float sy = static_cast<float>((h - icon) / 2);
            Gdiplus::RectF symbol(sx, sy, S, S);
            auto strokeRect = [&](const Gdiplus::RectF& r) {
                Gdiplus::RectF rr(r);
                rr.Inflate(-stroke * 0.5f, -stroke * 0.5f); // inset 0.5 (Electron)
                g.DrawRectangle(&pen, rr);
            };
            if (i == 0) { // minimizar: línea
                g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                g.DrawLine(&pen, sx, sy + S / 2.0f, sx + S, sy + S / 2.0f);
            } else if (i == 1 && !maximized) { // maximizar: cuadrado
                g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                strokeRect(symbol);
            } else if (i == 1) { // restaurar: dos cuadrados
                g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                int sepI = static_cast<int>(2.0 * static_cast<double>(dpi) / 96.0);
                if (sepI < 1) sepI = 2;
                const float sep = static_cast<float>(sepI);
                Gdiplus::RectF front(sx, sy + sep, S - sep, S - sep);
                strokeRect(front);
                g.SetClip(front, Gdiplus::CombineModeExclude);
                strokeRect(Gdiplus::RectF(sx + sep, sy, S - sep, S - sep));
                g.ResetClip();
            } else { // cerrar: X (AA, recortada)
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetClip(symbol);
                g.DrawLine(&pen, sx, sy, sx + S, sy + S);
                g.DrawLine(&pen, sx + S, sy, sx, sy + S);
                g.ResetClip();
            }
        }
    }

    RECT wr;
    GetWindowRect(hwnd, &wr);
    POINT dst{wr.left, wr.top}, src{0, 0};
    SIZE size{w, h};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd, screen, &dst, &size, mem, &src, 0, &bf, ULW_ALPHA);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
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
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        DrawCaptionBar(hwnd, pd);
        return 0;
    }
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
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&wc);
    done = true;
}

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

// ── plataforma: estado extendido (C9) ────────────────────────────────────────
namespace {
void ToggleWinStyle(HWND hwnd, LONG_PTR add, LONG_PTR remove) {
    LONG_PTR s = GetWindowLongPtrW(hwnd, GWL_STYLE);
    s = (s | add) & ~remove;
    SetWindowLongPtrW(hwnd, GWL_STYLE, s);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}
void ToggleWinExStyle(HWND hwnd, LONG_PTR add, LONG_PTR remove) {
    LONG_PTR s = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    s = (s | add) & ~remove;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, s);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}
} // namespace

bool Window::Impl::PIsVisible() const { return pdata && IsWindowVisible(pdata->hwnd); }
bool Window::Impl::PIsFocused() const {
    return pdata && pdata->hwnd && GetForegroundWindow() == pdata->hwnd;
}
bool Window::Impl::PIsResizable() const { return opts.resizable; }
bool Window::Impl::PIsMovable() const { return opts.movable; }
bool Window::Impl::PIsMinimizable() const { return opts.minimizable; }
bool Window::Impl::PIsMaximizable() const { return opts.maximizable; }
bool Window::Impl::PIsClosable() const { return opts.closable; }
bool Window::Impl::PIsAlwaysOnTop() const { return opts.alwaysOnTop; }
bool Window::Impl::PIsKiosk() const { return pdata && pdata->kiosk; }
bool Window::Impl::PIsDestroyed() const { return !pdata || !pdata->hwnd; }

void Window::Impl::PSetResizable(bool on) {
    opts.resizable = on;
    if (pdata && pdata->hwnd)
        ToggleWinStyle(pdata->hwnd, on ? WS_THICKFRAME : 0, on ? 0 : WS_THICKFRAME);
}
void Window::Impl::PSetMovable(bool on) { opts.movable = on; }
void Window::Impl::PSetMinimizable(bool on) {
    opts.minimizable = on;
    if (pdata && pdata->hwnd)
        ToggleWinStyle(pdata->hwnd, on ? WS_MINIMIZEBOX : 0, on ? 0 : WS_MINIMIZEBOX);
}
void Window::Impl::PSetMaximizable(bool on) {
    opts.maximizable = on;
    if (pdata && pdata->hwnd)
        ToggleWinStyle(pdata->hwnd, on ? WS_MAXIMIZEBOX : 0, on ? 0 : WS_MAXIMIZEBOX);
}
void Window::Impl::PSetClosable(bool on) {
    opts.closable = on;
    if (pdata && pdata->hwnd)
        ToggleWinStyle(pdata->hwnd, on ? WS_SYSMENU : 0, on ? 0 : WS_SYSMENU);
}
void Window::Impl::PSetAlwaysOnTop(bool on, int) {
    opts.alwaysOnTop = on;
    if (pdata && pdata->hwnd)
        SetWindowPos(pdata->hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE);
    EmitPlatformEvent(this, "alwaysOnTopChanged", on ? "true" : "false");
}
void Window::Impl::PSetSkipTaskbar(bool on) {
    opts.skipTaskbar = on;
    if (pdata && pdata->hwnd)
        ToggleWinExStyle(pdata->hwnd, on ? WS_EX_TOOLWINDOW : 0,
                         on ? 0 : WS_EX_TOOLWINDOW);
}
void Window::Impl::PSetHasShadow(bool on) { opts.hasShadow = on; }
void Window::Impl::PSetKiosk(bool on) {
    if (!pdata || !pdata->hwnd) return;
    pdata->kiosk = on;
    SetWindowPos(pdata->hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE);
    ShowWindow(pdata->hwnd, on ? SW_MAXIMIZE : SW_RESTORE);
}
void Window::Impl::PSetIgnoreMouseEvents(bool ignore, bool forward) {
    if (!pdata || !pdata->hwnd) return;
    LONG_PTR add = ignore ? WS_EX_TRANSPARENT : 0;
    LONG_PTR rem = ignore ? 0 : WS_EX_TRANSPARENT;
    if (ignore && forward) add |= WS_EX_LAYERED;
    ToggleWinExStyle(pdata->hwnd, add, rem);
}
void Window::Impl::PSetProgressBar(double value, const std::string& mode) {
    if (!pdata || !pdata->hwnd) return;
    static ITaskbarList3* s_tb = nullptr;
    if (!s_tb) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&s_tb))) ||
            !s_tb) {
            s_tb = nullptr;
            return;
        }
        s_tb->HrInit();
    }
    if (mode == "indeterminate" || value < 0) {
        s_tb->SetProgressState(pdata->hwnd, TBPF_INDETERMINATE);
        return;
    }
    TBPFLAG flag = value <= 0 ? TBPF_NOPROGRESS
                   : mode == "paused" ? TBPF_PAUSED
                   : mode == "error" ? TBPF_ERROR
                                      : TBPF_NORMAL;
    s_tb->SetProgressState(pdata->hwnd, flag);
    if (flag != TBPF_NOPROGRESS)
        s_tb->SetProgressValue(pdata->hwnd, static_cast<ULONGLONG>(value * 1000), 1000);
}
void Window::Impl::PSetBackgroundColor(const std::string& color) {
    opts.backgroundColor = color;
    if (color.size() < 7 || color[0] != '#' || !webview) return;
    int r = 0, g = 0, b = 0;
    try {
        r = std::stoi(color.substr(1, 2), nullptr, 16);
        g = std::stoi(color.substr(3, 2), nullptr, 16);
        b = std::stoi(color.substr(5, 2), nullptr, 16);
    } catch (...) {
        return;
    }
    webview->SetBackgroundColor(r, g, b, 255);
}
void Window::Impl::PMoveTop() {
    if (!pdata || !pdata->hwnd) return;
    SetWindowPos(pdata->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(pdata->hwnd);
}
void Window::Impl::PSetAspectRatio(double ratio, int extraW, int extraH) {
    if (pdata) pdata->aspect = ratio;
    opts.aspectRatio = ratio;
    opts.aspectExtraW = extraW;
    opts.aspectExtraH = extraH;
}
Window::Bounds Window::Impl::PGetContentBounds() const {
    Bounds b;
    if (!pdata || !pdata->hwnd) return b;
    RECT rc;
    GetClientRect(pdata->hwnd, &rc);
    POINT tl{rc.left, rc.top};
    ClientToScreen(pdata->hwnd, &tl);
    b.x = tl.x;
    b.y = tl.y;
    b.w = rc.right - rc.left;
    b.h = rc.bottom - rc.top;
    return b;
}
void Window::Impl::PSetContentSize(int w, int h) {
    if (!pdata || !pdata->hwnd) return;
    RECT rc{0, 0, w, h};
    AdjustWindowRectEx(&rc, static_cast<DWORD>(GetWindowLongPtrW(pdata->hwnd, GWL_STYLE)),
                       FALSE, static_cast<DWORD>(GetWindowLongPtrW(pdata->hwnd, GWL_EXSTYLE)));
    SetWindowPos(pdata->hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}
Window::Size Window::Impl::PGetContentSize() const {
    Size s;
    if (!pdata || !pdata->hwnd) return s;
    RECT rc;
    GetClientRect(pdata->hwnd, &rc);
    s.width = rc.right - rc.left;
    s.height = rc.bottom - rc.top;
    return s;
}
Window::Size Window::Impl::PGetMinimumSize() const {
    return Size{opts.minWidth, opts.minHeight};
}
Window::Size Window::Impl::PGetMaximumSize() const {
    return Size{opts.maxWidth, opts.maxHeight};
}
void Window::Impl::PSetMinimumSize(int w, int h) {
    opts.minWidth = w;
    opts.minHeight = h;
}
void Window::Impl::PSetMaximumSize(int w, int h) {
    opts.maxWidth = w;
    opts.maxHeight = h;
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

void Window::Impl::PSetApplicationMenu(const std::string& itemsJson) {
    if (!pdata || !pdata->hwnd) return;
    if (pdata->appMenu) {
        SetMenu(pdata->hwnd, nullptr);
        DestroyMenu(pdata->appMenu);
        pdata->appMenu = nullptr;
    }
    pdata->menuCmds.clear();

    auto parsed = ow::json::Parse(std::string_view(itemsJson));
    const ow::json::Value* items = nullptr;
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty()) {
        const ow::json::Value& a0 = parsed.value->AsArray()[0];
        items = a0.IsObject() ? a0.Find("items") : &a0;
    }
    if (!items || !items->IsArray()) {
        DrawMenuBar(pdata->hwnd);
        return;
    }
    HMENU bar = CreateMenu();
    UINT nextCmd = 20000;
    BuildMenuBar(bar, ow::menu::ParseItems(*items), pdata->menuCmds, nextCmd);
    pdata->appMenu = bar;
    SetMenu(pdata->hwnd, bar);
    DrawMenuBar(pdata->hwnd);
}

void Window::Impl::PSetColorScheme(int mode) {
    if (webview) webview->SetPreferredColorScheme(mode);
}

void Window::Impl::PPrintToPDF(std::function<void(bool, const std::string&)> cb) {
    if (webview) webview->PrintToPDF(std::move(cb));
    else if (cb) cb(false, {});
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
