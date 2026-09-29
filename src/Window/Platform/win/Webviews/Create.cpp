// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Webviews/Create.cpp — creacion de webviews hijas (WebView2).
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

} // namespace ow
