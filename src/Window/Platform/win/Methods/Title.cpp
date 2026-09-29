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
