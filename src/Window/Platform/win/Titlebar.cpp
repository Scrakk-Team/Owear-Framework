// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Titlebar.cpp — titleBarOverlay (botones nativos Win).
#include "Internal.hpp"
#include "PlatformData.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>

#include <string>

namespace ow {

int CaptionButtonAt(Window::Impl::PlatformData* pd, int x) {
    if (!pd || pd->capW <= 0) return -1;
    int i = (x * 3) / pd->capW;
    return (i < 0 || i > 2) ? -1 : i;
}


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



} // namespace ow
