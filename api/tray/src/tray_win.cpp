// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/tray/src/tray_win.cpp — Shell_NotifyIconW completo: icono (PNG→HICON con
// GDI+), tooltip, título, menú contextual (template de Menu) y eventos
// (click/right-click/double-click). Ventana fantasma propia recibe callbacks.
//
#ifndef UNICODE
#define UNICODE
#endif
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Menu.hpp"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <propidl.h>
#include <shellapi.h>

#include <gdiplus.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace tray {

using ow::json::Value;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

constexpr UINT kCallbackMsg = WM_APP + 80;

static const ow_module_host_t* g_host = nullptr;
static HWND s_hwnd = nullptr;
static NOTIFYICONDATAW s_nid{};
static bool s_added = false;
static HICON s_hicon = nullptr;
static HMENU s_menu = nullptr;
static std::map<UINT, std::pair<std::string, std::string>> s_menuCmds; // cmd→{id,role}

static void Emit(const char* name, const std::string& payload) {
    if (g_host && g_host->emit_event)
        g_host->emit_event(g_host->ctx, 0, name, payload.c_str());
}

// ── GDI+ (PNG → HICON) ──────────────────────────────────────────────────────
static void EnsureGdiplus() {
    static std::once_flag once;
    std::call_once(once, [] {
        ULONG_PTR token = 0;
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&token, &in, nullptr);
    });
}

static HICON PngToHicon(const std::string& b64) {
    std::vector<uint8_t> png;
    if (!ow::b64::Decode(b64, png) || png.empty()) return nullptr;
    EnsureGdiplus();
    IStream* s = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &s)) || !s) return nullptr;
    ULONG written = 0;
    s->Write(png.data(), static_cast<ULONG>(png.size()), &written);
    LARGE_INTEGER z{};
    z.QuadPart = 0;
    s->Seek(z, STREAM_SEEK_SET, nullptr);
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(s);
    HICON hicon = nullptr;
    if (bmp) {
        bmp->GetHICON(&hicon);
        delete bmp;
    }
    s->Release();
    return hicon;
}

// ── menú contextual ─────────────────────────────────────────────────────────
static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

static void AppendItems(HMENU m, const std::vector<ow::menu::Item>& items, UINT& nextCmd) {
    for (const auto& it : items) {
        if (!it.visible) continue;
        if (it.type == "separator") {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        const std::wstring label =
            ToWide(it.label + (it.accelerator.empty() ? "" : ("\t" + it.accelerator)));
        if (!it.submenu.empty()) {
            HMENU sub = CreatePopupMenu();
            AppendItems(sub, it.submenu, nextCmd);
            AppendMenuW(m, MF_POPUP | (it.enabled ? 0 : MF_GRAYED),
                        reinterpret_cast<UINT_PTR>(sub), label.c_str());
        } else {
            UINT flags = MF_STRING;
            if (!it.enabled) flags |= MF_GRAYED;
            if (it.checked) flags |= MF_CHECKED;
            const UINT cmd = nextCmd++;
            s_menuCmds[cmd] = {it.id, it.role};
            AppendMenuW(m, flags, cmd, label.c_str());
            if (it.type == "radio") {
                MENUITEMINFOW mii{};
                mii.cbSize = sizeof(mii);
                mii.fMask = MIIM_FTYPE;
                mii.fType = MFT_RADIOCHECK;
                SetMenuItemInfoW(m, cmd, FALSE, &mii);
            }
        }
    }
}

static void RebuildMenu(const Value& items) {
    if (s_menu) {
        DestroyMenu(s_menu);
        s_menu = nullptr;
    }
    s_menuCmds.clear();
    s_menu = CreatePopupMenu();
    UINT nextCmd = 30000;
    AppendItems(s_menu, ow::menu::ParseItems(items), nextCmd);
}

static void ShowContextMenu() {
    if (!s_menu || !s_hwnd) return;
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(s_hwnd);
    int cmd = TrackPopupMenuEx(s_menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                               pt.x, pt.y, s_hwnd, nullptr);
    PostMessageW(s_hwnd, WM_NULL, 0, 0);
    if (cmd >= 30000) {
        auto it = s_menuCmds.find(static_cast<UINT>(cmd));
        if (it != s_menuCmds.end()) {
            ow::menu::Item mi;
            mi.id = it->second.first;
            mi.role = it->second.second;
            Emit("menu.click", ow::menu::ClickPayload(mi, 0));
        }
    }
}

// ── ventana fantasma ────────────────────────────────────────────────────────
static LRESULT CALLBACK TrayWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == kCallbackMsg) {
        switch (lp) {
        case WM_LBUTTONUP: Emit("tray.event", "{\"button\":\"left\"}"); break;
        case WM_LBUTTONDBLCLK: Emit("tray.event", "{\"button\":\"double\"}"); break;
        case WM_RBUTTONUP:
            Emit("tray.event", "{\"button\":\"right\"}");
            ShowContextMenu();
            break;
        case WM_CONTEXTMENU:
            ShowContextMenu();
            break;
        default: break;
        }
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static bool EnsureWindow() {
    if (s_hwnd) return true;
    WNDCLASSW wc{};
    wc.lpfnWndProc = &TrayWndProc;
    wc.lpszClassName = L"owear-tray";
    wc.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    s_hwnd = CreateWindowExW(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE,
                             nullptr, wc.hInstance, nullptr);
    return s_hwnd != nullptr;
}

// ── API ─────────────────────────────────────────────────────────────────────
void create(const ow_request_t*, ow_response_t* res) {
    if (s_added) return RespondOk(res, "\"owear-tray\"");
    if (!EnsureWindow()) return RespondError(res, "no se pudo crear la ventana del tray");

    s_nid.cbSize = sizeof(s_nid);
    s_nid.hWnd = s_hwnd;
    s_nid.uID = 1;
    s_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    s_nid.uCallbackMessage = kCallbackMsg;
    s_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy(s_nid.szTip, L"Owear", 127);
    if (!Shell_NotifyIconW(NIM_ADD, &s_nid))
        return RespondError(res, "Shell_NotifyIcon(NIM_ADD) falló");
    s_added = true;
    RespondOk(res, "\"owear-tray\"");
}

void setImage(const ow_request_t* req, ow_response_t* res) {
    if (!s_added) return RespondError(res, "tray.create primero");
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "se espera [pngBase64]");
    HICON icon = PngToHicon(parsed.value->AsArray()[0].AsString());
    if (!icon) return RespondError(res, "PNG inválido");

    s_nid.uFlags = NIF_ICON;
    s_nid.hIcon = icon;
    if (!Shell_NotifyIconW(NIM_MODIFY, &s_nid)) {
        DestroyIcon(icon);
        return RespondError(res, "Shell_NotifyIcon(NIM_MODIFY) falló");
    }
    if (s_hicon) DestroyIcon(s_hicon);
    s_hicon = icon;
    RespondOk(res, "null");
}

void setPressedImage(const ow_request_t*, ow_response_t* res) {
    RespondOk(res, "null"); // no aplicable en Windows
}

void setToolTip(const ow_request_t* req, ow_response_t* res) {
    if (!s_added) return RespondError(res, "tray.create primero");
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    std::string tip;
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString())
        tip = parsed.value->AsArray()[0].AsString();
    s_nid.uFlags = NIF_TIP;
    wcsncpy(s_nid.szTip, ToWide(tip).c_str(), 127);
    s_nid.szTip[127] = L'\0';
    if (!Shell_NotifyIconW(NIM_MODIFY, &s_nid))
        return RespondError(res, "Shell_NotifyIcon(NIM_MODIFY) falló");
    RespondOk(res, "null");
}

void setTitle(const ow_request_t* req, ow_response_t* res) { setToolTip(req, res); }

void setContextMenu(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "items requeridos");
    const Value& a0 = parsed.value->AsArray()[0];
    const Value* items = a0.IsObject() ? a0.Find("items") : &a0;
    if (!items || !items->IsArray()) return RespondError(res, "items requeridos");
    RebuildMenu(*items);
    RespondOk(res, "null");
}

void popupContextMenu(const ow_request_t*, ow_response_t* res) {
    ShowContextMenu();
    RespondOk(res, "null");
}

void destroy(const ow_request_t*, ow_response_t* res) {
    if (s_added) {
        Shell_NotifyIconW(NIM_DELETE, &s_nid);
        s_added = false;
    }
    if (s_hicon) {
        DestroyIcon(s_hicon);
        s_hicon = nullptr;
    }
    if (s_menu) {
        DestroyMenu(s_menu);
        s_menu = nullptr;
    }
    RespondOk(res, "null");
}

} // namespace tray

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    tray::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"create", &tray::create},
        {"setImage", &tray::setImage},
        {"setPressedImage", &tray::setPressedImage},
        {"setToolTip", &tray::setToolTip},
        {"setTitle", &tray::setTitle},
        {"setContextMenu", &tray::setContextMenu},
        {"popupContextMenu", &tray::popupContextMenu},
        {"destroy", &tray::destroy},
    };
    static const ow_module_desc_t d{"tray", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
