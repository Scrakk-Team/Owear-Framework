// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Methods.cpp — estado/geometria/titulo/drags (C9, Win32).
#include "../Internal.hpp"
#include "../PlatformData.hpp"

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

} // namespace ow
