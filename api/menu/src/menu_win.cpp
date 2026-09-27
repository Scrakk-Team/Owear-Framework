// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/menu/src/menu_win.cpp — menú contextual nativo (TrackPopupMenu) con
// template estilo Electron. Hilo de UI dedicado (los menús Win32 exigen ventana
// y bomba propias). El click se emite como `menu.click {id, role, windowId}`
// (al SDK del main y a los renderers). setApplicationMenu es noop aquí: el
// menubar de aplicación lo aplica el kernel vía window.setApplicationMenu.
//
#include "ow/Menu.hpp"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace menu {
using ow::menu::Item;
using ow::menu::ParseItems;
using ow::menu::ClickPayload;

using ow::json::Value;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;

constexpr UINT kMsgPopup = WM_APP + 85;

static HWND s_hwnd = nullptr;
static std::mutex s_mu;
static std::condition_variable s_cv;

struct PopupCmd {
    std::string json;
    uint32_t win = 0;
    bool done = false;
    Item chosen;
    bool hasChosen = false;
};

static std::vector<Item> s_cmdItems; // cmd-1000 → Item

static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static void AppendItems(HMENU m, const std::vector<Item>& items) {
    for (const Item& it : items) {
        if (!it.visible) continue;
        if (it.type == "separator") {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        const std::wstring disp =
            ToWide(it.label + (it.accelerator.empty() ? "" : ("\t" + it.accelerator)));
        if (!it.submenu.empty()) {
            HMENU sm = CreatePopupMenu();
            AppendItems(sm, it.submenu);
            AppendMenuW(m, MF_POPUP | (it.enabled ? 0 : MF_GRAYED),
                        reinterpret_cast<UINT_PTR>(sm), disp.c_str());
            continue;
        }
        UINT flags = MF_STRING;
        if (!it.enabled) flags |= MF_GRAYED;
        if (it.checked) flags |= MF_CHECKED;
        const UINT_PTR cmd = s_cmdItems.size() + 1000;
        s_cmdItems.push_back(it);
        AppendMenuW(m, flags, cmd, disp.c_str());
        if (it.type == "radio") {
            MENUITEMINFOW mii{};
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_FTYPE;
            mii.fType = MFT_RADIOCHECK;
            SetMenuItemInfoW(m, cmd, FALSE, &mii);
        }
    }
}

static LRESULT CALLBACK MenuWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kMsgPopup) {
        auto* c = reinterpret_cast<PopupCmd*>(lp);
        HMENU hm = CreatePopupMenu();
        s_cmdItems.clear();
        auto parsed = Parse(c->json);
        const Value* items = nullptr;
        if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty()) {
            const Value& a0 = parsed.value->AsArray()[0];
            items = a0.IsObject() ? a0.Find("items") : &a0;
        }
        if (items && items->IsArray()) AppendItems(hm, ParseItems(*items));

        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(h);
        int cmd = TrackPopupMenuEx(hm, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                   pt.x, pt.y, h, nullptr);
        PostMessageW(h, WM_NULL, 0, 0);
        DestroyMenu(hm);
        if (cmd >= 1000 && (size_t)(cmd - 1000) < s_cmdItems.size()) {
            c->chosen = s_cmdItems[cmd - 1000];
            c->hasChosen = true;
        }
        {
            std::lock_guard lk(s_mu);
            c->done = true;
        }
        s_cv.notify_all();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void MenuThread() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &MenuWndProc;
    wc.lpszClassName = L"owear-menu";
    wc.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    s_hwnd = CreateWindowExW(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE,
                             nullptr, wc.hInstance, nullptr);
    MSG msg;
    while (GetMessage(&msg, s_hwnd, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// args: [ { items: [...], x?, y? } ]  (o legacy: [ items ])
void popup(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "args inválidos");
    const Value& a0 = parsed.value->AsArray()[0];
    const Value* items = a0.IsObject() ? a0.Find("items") : &a0;
    if (!items || !items->IsArray()) return RespondError(res, "items requeridos");

    static std::once_flag once;
    std::call_once(once, [] { std::thread(MenuThread).detach(); });
    for (int i = 0; i < 100 && !s_hwnd; ++i) Sleep(10);
    if (!s_hwnd) return RespondError(res, "hilo de menú no disponible");

    PopupCmd c;
    c.json = std::string(req->json, req->json_len);
    c.win = req->window_id;
    PostMessageW(s_hwnd, kMsgPopup, 0, reinterpret_cast<LPARAM>(&c));
    {
        std::unique_lock lk(s_mu);
        s_cv.wait(lk, [&] { return c.done; });
    }

    if (c.hasChosen && g_host && g_host->emit_event) {
        const std::string payload = ClickPayload(c.chosen, c.win);
        // window_id = 0 → SDK (main) + todas las ventanas
        g_host->emit_event(g_host->ctx, 0, "menu.click", payload.c_str());
    }
    RespondOk(res, "null");
}

} // namespace menu

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    menu::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"popup", &menu::popup},
        {"setApplicationMenu",
         [](const ow_request_t*, ow_response_t* res) {
             ow::Module::RespondOk(res, "\"aplicado-por-kernel\"");
         }},
    };
    static const ow_module_desc_t d{"menu", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
