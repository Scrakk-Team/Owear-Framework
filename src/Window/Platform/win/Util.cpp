// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Util.cpp — helpers compartidos (UTF-8/UTF-16, HWND->Impl).
#include "Internal.hpp"
#include "PlatformData.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <map>
#include <string>
#include <string_view>

namespace ow {

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

} // namespace ow
