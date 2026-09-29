// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Webviews/Commands.cpp — operaciones sobre webviews hijas.
#include "../Internal.hpp"
#include "../PlatformData.hpp"
#include "ow/detail/minjson.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <map>
#include <string>
#include <vector>

#include "Internal.hpp"
namespace ow {

// ── webviews embebidas (Windows / WebView2) ─────────────────────────────────

using namespace Microsoft::WRL;
using namespace webview_win_detail;

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
