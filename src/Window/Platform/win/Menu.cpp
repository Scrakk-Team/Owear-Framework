// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Menu.cpp — menubar de aplicacion (C5, Win32 HMENU).
#include "Internal.hpp"
#include "PlatformData.hpp"
#include "ow/Menu.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

namespace ow {

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

} // namespace ow
