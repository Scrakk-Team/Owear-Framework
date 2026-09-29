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

namespace ow {

// ── webviews embebidas (Windows / WebView2) ─────────────────────────────────
#if OW_HAS_WEBVIEW2

using namespace Microsoft::WRL;

namespace {

std::wstring ViewUserDataDir() {
    PWSTR local = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                       &local))) {
        dir = std::wstring(local) + L"\\owear\\WebView2-views";
        CoTaskMemFree(local);
    } else {
        dir = L".\\owear-webview2-views";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void EmitViewJson(Window::Impl* impl, const char* name, uint32_t id,
                  const char* key, std::string val) {
    json::Object o;
    o.emplace_back("id", json::Value(static_cast<int64_t>(id)));
    o.emplace_back(key, json::Value(std::move(val)));
    Window::Impl::EmitPlatformEvent(impl, name,
                                    json::Value(std::move(o)).Serialize());
}

void ApplyViewBounds(Window::Impl::PlatformData* pd, uint32_t id) {
    auto it = pd->views.find(id);
    if (it == pd->views.end()) return;
    if (it->second.host) {
        SetWindowPos(it->second.host, HWND_TOP, it->second.x, it->second.y,
                     it->second.w, it->second.h, SWP_NOACTIVATE);
    }
    if (it->second.ctrl) {
        RECT rc{0, 0, it->second.w, it->second.h}; // relativo al host
        if (!it->second.host)
            rc = {it->second.x, it->second.y, it->second.x + it->second.w,
                  it->second.y + it->second.h};
        it->second.ctrl->put_Bounds(rc);
    }
}

void WireViewEvents(Window::Impl* impl, Window::Impl::PlatformData* pd,
                    uint32_t id) {
    auto it = pd->views.find(id);
    if (it == pd->views.end() || !it->second.view) return;
    ICoreWebView2* v = it->second.view.Get();

    v->add_SourceChanged(
        Callback<ICoreWebView2SourceChangedEventHandler>(
            [impl, id](ICoreWebView2* s, ICoreWebView2SourceChangedEventArgs*)
                -> HRESULT {
                LPWSTR u = nullptr;
                if (SUCCEEDED(s->get_Source(&u)) && u) {
                    EmitViewJson(impl, "webview.urlChanged", id, "url", WideToUtf8(u));
                    CoTaskMemFree(u);
                }
                return S_OK;
            }).Get(), nullptr);
    v->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [impl, id](ICoreWebView2* s, IUnknown*) -> HRESULT {
                LPWSTR t = nullptr;
                if (SUCCEEDED(s->get_DocumentTitle(&t)) && t) {
                    EmitViewJson(impl, "webview.titleChanged", id, "title",
                                 WideToUtf8(t));
                    CoTaskMemFree(t);
                }
                return S_OK;
            }).Get(), nullptr);
    v->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [impl, id](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*)
                -> HRESULT {
                EmitViewJson(impl, "webview.loadChanged", id, "state", "started");
                return S_OK;
            }).Get(), nullptr);
    v->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [impl, id](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a)
                -> HRESULT {
                BOOL ok = FALSE;
                if (a) a->get_IsSuccess(&ok);
                if (ok) {
                    EmitViewJson(impl, "webview.loadChanged", id, "state", "finished");
                } else {
                    COREWEBVIEW2_WEB_ERROR_STATUS st =
                        COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                    if (a) a->get_WebErrorStatus(&st);
                    EmitViewJson(impl, "webview.loadFailed", id, "message",
                                 "WebErrorStatus " + std::to_string((int)st));
                }
                return S_OK;
            }).Get(), nullptr);
}

void CreateViewController(Window::Impl* impl, Window::Impl::PlatformData* pd,
                          uint32_t id) {
    if (!pd->viewEnv || !pd->hwnd) return;
    auto itv = pd->views.find(id);
    HWND parent = (itv != pd->views.end() && itv->second.host)
                      ? itv->second.host
                      : pd->hwnd;
    pd->viewEnv->CreateCoreWebView2Controller(
        parent,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [impl, pd, id](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
                if (FAILED(hr) || !c) {
                    log::Error("webview", "controller de hija falló hr=" +
                        std::to_string(static_cast<unsigned long>(hr)));
                    return E_FAIL;
                }
                auto it = pd->views.find(id);
                if (it == pd->views.end()) { c->Release(); return S_OK; }
                it->second.ctrl = c;
                it->second.ctrl->get_CoreWebView2(&it->second.view);
                it->second.ready = true;
                ApplyViewBounds(pd, id);
                it->second.ctrl->put_IsVisible(it->second.visible ? TRUE : FALSE);
                if (!it->second.userAgent.empty()) {
                    ComPtr<ICoreWebView2Settings> st;
                    if (SUCCEEDED(it->second.view->get_Settings(&st)) && st) {
                        ComPtr<ICoreWebView2Settings2> st2;
                        if (SUCCEEDED(st.As(&st2)) && st2)
                            st2->put_UserAgent(
                                Utf8ToWide(it->second.userAgent).c_str());
                    }
                }
                if (it->second.transparent) {
                    ComPtr<ICoreWebView2Controller2> c2;
                    if (SUCCEEDED(it->second.ctrl.As(&c2))) {
                        COREWEBVIEW2_COLOR col{};
                        col.A = 0; col.R = 0; col.G = 0; col.B = 0;
                        c2->put_DefaultBackgroundColor(col);
                    }
                }
                WireViewEvents(impl, pd, id);
                if (!it->second.pendingUrl.empty()) {
                    std::string u = it->second.pendingUrl;
                    it->second.pendingUrl.clear();
                    it->second.view->Navigate(Utf8ToWide(u).c_str());
                }
                return S_OK;
            }).Get());
}

void EnsureViewEnv(Window::Impl* impl, Window::Impl::PlatformData* pd) {
    if (pd->viewEnv || pd->viewEnvCreating) return;
    pd->viewEnvCreating = true;
    std::wstring dir = ViewUserDataDir();
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, dir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [impl, pd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) {
                    log::Error("webview", "environment de hijas falló");
                    return E_FAIL;
                }
                pd->viewEnv = env;
                pd->viewEnvReady = true;
                auto pending = pd->pendingViewCreate;
                pd->pendingViewCreate.clear();
                for (uint32_t id : pending) CreateViewController(impl, pd, id);
                return S_OK;
            }).Get());
    if (FAILED(hr))
        log::Error("webview",
                   "CreateCoreWebView2EnvironmentWithOptions (hijas) falló");
}

} // namespace

#endif // OW_HAS_WEBVIEW2

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
