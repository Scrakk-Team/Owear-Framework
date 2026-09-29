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


} // namespace ow
