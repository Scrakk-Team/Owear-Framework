// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Methods.cpp — estado/geometria/titulo/drags (C9, Win32).
#include "Internal.hpp"
#include "PlatformData.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

#include <string>

namespace ow {

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


} // namespace ow
