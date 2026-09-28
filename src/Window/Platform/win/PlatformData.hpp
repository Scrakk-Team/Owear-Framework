// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/PlatformData.hpp — datos de plataforma (Win32) de Window.
#pragma once

#include "ow/Window.h"
#include "../../Window_p.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef OEMRESOURCE
#define OEMRESOURCE
#endif
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>

#include <map>
#include <string>
#include <vector>

#include <objbase.h>

#if __has_include(<WebView2.h>)
#include <WebView2.h>
#include <wrl/client.h>
#define OW_PD_HAS_WEBVIEW2 1
#else
#define OW_PD_HAS_WEBVIEW2 0
#endif

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

#if OW_PD_HAS_WEBVIEW2
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
} // namespace ow
