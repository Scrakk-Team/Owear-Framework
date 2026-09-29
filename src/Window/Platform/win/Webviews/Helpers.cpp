// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Webviews/Helpers.cpp — creacion/eventos de webviews hijas.
#include "Internal.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <string>

namespace ow {
#if OW_HAS_WEBVIEW2

using namespace Microsoft::WRL;

namespace webview_win_detail {

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

} // namespace webview_win_detail
#endif // OW_HAS_WEBVIEW2
} // namespace ow
