// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/win/Helpers.cpp — helpers del backend WebView2 (UTF-16, perfil, args).
#include "Internal.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ow {
namespace webview2_detail {

std::wstring Utf8ToWide(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// Perfil de usuario fuera del dir del exe (puede ser read-only en installs
// de sistema) — patrón GetUserDataDir de ole/browser_host.
// `partition` aísla cookies/storage por perfil (session API).
std::wstring UserDataDir(const std::string& partition) {
    PWSTR local = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                       &local))) {
        dir = std::wstring(local) + L"\\owear\\WebView2";
        CoTaskMemFree(local);
    } else {
        dir = L".\\owear-webview2";
    }
    if (!partition.empty()) {
        std::string safe = partition;
        for (char& c : safe)
            if (c == ':' || c == '/' || c == '\\' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        dir += L"\\" + Utf8ToWide(safe);
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec); // no-lanzante
    return dir;
}

/// Extrae `ow-partition=<nombre>` de los args del WebView.
std::string PartitionFromArgs(const std::vector<std::string>& args) {
    static const std::string kPrefix = "ow-partition=";
    for (const auto& a : args) {
        if (a.rfind(kPrefix, 0) == 0 && a.size() > kPrefix.size())
            return a.substr(kPrefix.size());
    }
    return {};
}

} // namespace webview2_detail
} // namespace ow
