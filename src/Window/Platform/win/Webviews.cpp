// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Webviews.cpp — webviews embebidas (WebView2, Windows).
#include "Internal.hpp"
#include "PlatformData.hpp"
#include "ow/detail/minjson.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <map>
#include <string>
#include <vector>

#include "Webviews/Internal.hpp"
namespace ow {

// ── webviews embebidas (Windows / WebView2) ─────────────────────────────────

using namespace Microsoft::WRL;
using namespace webview_win_detail;

std::string Window::Impl::PCreateWebview(const std::string& optionsJson) {
#if OW_HAS_WEBVIEW2
    if (!pdata || !pdata->hwnd) return "{\"message\":\"sin ventana\"}";

    auto parsed = json::Parse(optionsJson);
    const json::Value& o = parsed.value ? *parsed.value : json::Value(nullptr);
    std::string url, ua;
    int x = 0, y = 0, w = 320, h = 240;
    bool transparent = false;
    if (o.IsObject()) {
        if (const auto* v = o.Find("url"); v && v->IsString()) url = v->AsString();
        if (const auto* v = o.Find("x"); v && v->IsNumber()) x = (int)v->AsInt();
        if (const auto* v = o.Find("y"); v && v->IsNumber()) y = (int)v->AsInt();
        if (const auto* v = o.Find("width"); v && v->IsNumber())
            w = (int)v->AsInt();
        if (const auto* v = o.Find("height"); v && v->IsNumber())
            h = (int)v->AsInt();
        if (const auto* v = o.Find("transparent"); v && v->IsBool())
            transparent = v->AsBool();
        if (const auto* v = o.Find("userAgent"); v && v->IsString())
            ua = v->AsString();
    }

    const uint32_t id = pdata->nextViewId++;
    PlatformData::EmbeddedView ev;
    ev.x = x; ev.y = y; ev.w = w; ev.h = h;
    ev.transparent = transparent; ev.userAgent = ua; ev.pendingUrl = url;
    pdata->views[id] = std::move(ev);

    // HWND hijo propio: el controller se parenta aquí (z-order y bounds fiables
    // por encima del webview principal, sin depender del orden interno de WebView2).
    HWND host = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, x, y, w, h,
        pdata->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (host) {
        pdata->views[id].host = host;
        SetWindowPos(host, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
    }

    if (pdata->viewEnvReady)
        CreateViewController(this, pdata, id);
    else {
        pdata->pendingViewCreate.push_back(id);
        EnsureViewEnv(this, pdata);
    }

    json::Object r;
    r.emplace_back("id", json::Value(static_cast<int64_t>(id)));
    return json::Value(std::move(r)).Serialize();
#else
    (void)optionsJson;
    return {};
#endif
}

std::string Window::Impl::PWebviewCommand(uint32_t id, const std::string& op,
                                          const std::string& argsJson) {
#if OW_HAS_WEBVIEW2
    if (!pdata) return "{}";
    auto it = pdata->views.find(id);
    if (it == pdata->views.end())
        return "{\"message\":\"webview no encontrada\"}";
    PlatformData::EmbeddedView& ev = it->second;

    auto parsed = json::Parse(argsJson);
    const json::Value& a = parsed.value ? *parsed.value : json::Value(nullptr);
    auto argStr = [&](size_t i) -> std::string {
        if (a.IsArray() && a.AsArray().size() > i && a.AsArray()[i].IsString())
            return a.AsArray()[i].AsString();
        if (i == 0 && a.IsString()) return a.AsString();
        return {};
    };
    auto argBool = [&](size_t i, bool def) -> bool {
        if (a.IsArray() && a.AsArray().size() > i && a.AsArray()[i].IsBool())
            return a.AsArray()[i].AsBool();
        if (i == 0 && a.IsBool()) return a.AsBool();
        return def;
    };
    auto argNum = [&](size_t i, double def) -> double {
        if (a.IsArray() && a.AsArray().size() > i && a.AsArray()[i].IsNumber())
            return a.AsArray()[i].AsDouble();
        if (i == 0 && a.IsNumber()) return a.AsDouble();
        return def;
    };

    if (op == "setBounds") {
        const json::Value* src = nullptr;
        if (a.IsObject())
            src = &a;
        else if (a.IsArray() && !a.AsArray().empty() && a.AsArray()[0].IsObject())
            src = &a.AsArray()[0];
        if (src) {
            if (const auto* v = src->Find("x"); v && v->IsNumber()) ev.x = (int)v->AsInt();
            if (const auto* v = src->Find("y"); v && v->IsNumber()) ev.y = (int)v->AsInt();
            if (const auto* v = src->Find("width"); v && v->IsNumber())
                ev.w = (int)v->AsInt();
            if (const auto* v = src->Find("height"); v && v->IsNumber())
                ev.h = (int)v->AsInt();
        } else if (a.IsArray() && a.AsArray().size() >= 4) {
            ev.x = (int)a.AsArray()[0].AsInt();
            ev.y = (int)a.AsArray()[1].AsInt();
            ev.w = (int)a.AsArray()[2].AsInt();
            ev.h = (int)a.AsArray()[3].AsInt();
        }
        ApplyViewBounds(pdata, id);
        return "null";
    }
    if (op == "load") {
        std::string url = argStr(0);
        if (url.empty()) return "{\"message\":\"url requerida\"}";
        if (!ev.view) { ev.pendingUrl = url; return "null"; }
        ev.view->Navigate(Utf8ToWide(url).c_str());
        return "null";
    }
    if (op == "back") { if (ev.view) ev.view->GoBack(); return "null"; }
    if (op == "forward") { if (ev.view) ev.view->GoForward(); return "null"; }
    if (op == "reload") { if (ev.view) ev.view->Reload(); return "null"; }
    if (op == "stop") { if (ev.view) ev.view->Stop(); return "null"; }
    if (op == "canBack") {
        BOOL b = FALSE; if (ev.view) ev.view->get_CanGoBack(&b);
        return b ? "true" : "false";
    }
    if (op == "canForward") {
        BOOL b = FALSE; if (ev.view) ev.view->get_CanGoForward(&b);
        return b ? "true" : "false";
    }
    if (op == "getURL") {
        LPWSTR u = nullptr; std::string s;
        if (ev.view && SUCCEEDED(ev.view->get_Source(&u)) && u) {
            s = WideToUtf8(u); CoTaskMemFree(u);
        }
        return json::Value(s).Serialize();
    }
    if (op == "getTitle") {
        LPWSTR t = nullptr; std::string s;
        if (ev.view && SUCCEEDED(ev.view->get_DocumentTitle(&t)) && t) {
            s = WideToUtf8(t); CoTaskMemFree(t);
        }
        return json::Value(s).Serialize();
    }
    if (op == "eval") {
        std::string js = argStr(0);
        if (js.empty()) return "{\"message\":\"js requerido\"}";
        if (ev.view) ev.view->ExecuteScript(Utf8ToWide(js).c_str(), nullptr);
        return "null";
    }
    if (op == "setVisible") {
        ev.visible = argBool(0, true);
        if (ev.ctrl) ev.ctrl->put_IsVisible(ev.visible ? TRUE : FALSE);
        if (ev.host) {
            if (ev.visible)
                SetWindowPos(ev.host, HWND_TOP, ev.x, ev.y, ev.w, ev.h,
                             SWP_NOACTIVATE | SWP_SHOWWINDOW);
            else
                ShowWindow(ev.host, SW_HIDE);
        }
        return "null";
    }
    if (op == "setZoom") {
        if (ev.ctrl) ev.ctrl->put_ZoomFactor(argNum(0, 1.0));
        return "null";
    }
    if (op == "devtools") { if (ev.view) ev.view->OpenDevToolsWindow(); return "null"; }
    if (op == "findInPage") {
        std::string text = argStr(0);
        bool back = argBool(1, false);
        if (ev.view) {
            std::string js = "window.find(" + json::Value(text).Serialize() +
                             (back ? ",false,true" : ",false,false") + ")";
            ev.view->ExecuteScript(Utf8ToWide(js).c_str(), nullptr);
        }
        return "null";
    }
    if (op == "findStop") return "null";
    if (op == "destroy") {
        if (ev.ctrl) ev.ctrl->Close();
        if (ev.host) DestroyWindow(ev.host);
        pdata->views.erase(it);
        return "null";
    }
    return "{\"message\":\"op desconocida: " + op + "\"}";
#else
    (void)id; (void)op; (void)argsJson;
    return {};
#endif
}

} // namespace ow
