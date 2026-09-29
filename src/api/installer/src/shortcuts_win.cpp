// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/api/installer/src/shortcuts_win.cpp — integración de escritorio (Windows).
#include "Internal.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <string>
#include <vector>

namespace installermod::platform {

static std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

static std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) {
        out = p;
        CoTaskMemFree(p);
    }
    return out;
}

static bool MakeLink(const std::wstring& linkPath, const std::wstring& target,
                     const std::wstring& icon) {
    if (linkPath.empty() || target.empty()) return false;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, reinterpret_cast<void**>(&link))))
        return false;
    link->SetPath(target.c_str());
    const size_t slash = target.find_last_of(L"\\/");
    const std::wstring wDir = slash == std::wstring::npos ? std::wstring() : target.substr(0, slash);
    if (!wDir.empty()) link->SetWorkingDirectory(wDir.c_str());
    if (!icon.empty()) link->SetIconLocation(icon.c_str(), 0);
    bool ok = false;
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile,
                                       reinterpret_cast<void**>(&pf)))) {
        ok = SUCCEEDED(pf->Save(linkPath.c_str(), TRUE));
        pf->Release();
    }
    link->Release();
    return ok;
}

static void RegStr(HKEY root, const std::wstring& sub, const wchar_t* name,
                   const std::wstring& value) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k,
                        nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(k, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

bool CreateShortcuts(const std::string& appId, const std::string& appName,
                     const std::string& execPath, const std::string& iconPath,
                     bool desktop, bool menu, bool startup) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring wExec = W(execPath);
    const std::wstring wIcon = W(iconPath);
    const std::wstring wName = W(appName);
    if (wExec.empty()) return false;
    bool ok = true;
    if (desktop) {
        auto dir = KnownFolder(FOLDERID_Desktop);
        if (!dir.empty()) ok = MakeLink(dir + L"\\" + wName + L".lnk", wExec, wIcon) && ok;
    }
    if (menu) {
        auto dir = KnownFolder(FOLDERID_Programs);
        if (!dir.empty()) ok = MakeLink(dir + L"\\" + wName + L".lnk", wExec, wIcon) && ok;
    }
    if (startup) {
        auto dir = KnownFolder(FOLDERID_Startup);
        if (!dir.empty())
            ok = MakeLink(dir + L"\\" + W(appId) + L".lnk", wExec, wIcon) && ok;
    }
    return ok;
}

bool RemoveShortcuts(const std::string& appId, const std::string& appName) {
    const std::wstring name = W(appName);
    const std::wstring id = W(appId);
    for (REFKNOWNFOLDERID fid : {FOLDERID_Desktop, FOLDERID_Programs}) {
        auto dir = KnownFolder(fid);
        if (!dir.empty()) DeleteFileW((dir + L"\\" + name + L".lnk").c_str());
    }
    auto startup = KnownFolder(FOLDERID_Startup);
    if (!startup.empty()) DeleteFileW((startup + L"\\" + id + L".lnk").c_str());
    return true;
}

bool RegisterUninstall(const std::string& appId, const std::string& appName,
                       const std::string& version, const std::string& publisher,
                       const std::string& installDir, const std::string& uninstaller) {
    const std::wstring sub =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + W(appId);
    RegStr(HKEY_CURRENT_USER, sub, L"DisplayName", W(appName));
    RegStr(HKEY_CURRENT_USER, sub, L"DisplayVersion", W(version));
    RegStr(HKEY_CURRENT_USER, sub, L"Publisher", W(publisher));
    RegStr(HKEY_CURRENT_USER, sub, L"InstallLocation", W(installDir));
    RegStr(HKEY_CURRENT_USER, sub, L"UninstallString", W(uninstaller));
    RegStr(HKEY_CURRENT_USER, sub, L"DisplayIcon", W(uninstaller));
    return true;
}

bool UnregisterUninstall(const std::string& appId) {
    const std::wstring sub =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + W(appId);
    RegDeleteTreeW(HKEY_CURRENT_USER, sub.c_str());
    return true;
}

bool LaunchDetached(const std::string& path, const std::vector<std::string>& args) {
    if (path.empty()) return false;
    std::wstring cmd = W(path);
    for (const auto& a : args) cmd += L" " + W(a);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elev{};
    DWORD sz = sizeof(elev);
    bool ok = GetTokenInformation(token, TokenElevation, &elev, sz, &sz) && elev.TokenIsElevated;
    CloseHandle(token);
    return ok;
}

} // namespace installermod::platform
