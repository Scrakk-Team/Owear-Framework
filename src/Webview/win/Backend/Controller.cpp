// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/win/Backend/Controller.cpp — setup del ICoreWebView2Controller.
#include "../Webview2Backend.hpp"

namespace ow {

void Webview2Backend::OnControllerReady() {
        // scripts de init encolados antes de que existiera el webview
        for (const auto& js : pendingInitScripts_)
            webview_->AddScriptToExecuteOnDocumentCreated(Utf8ToWide(js).c_str(),
                                                          nullptr);
        pendingInitScripts_.clear();

        // mensajes JS→nativo (texto crudo)
        if (messageHandler_) AttachMessageHandler();

        // assets locales
        if (!assetRoot_.empty()) AttachAssetMapping();

        // protocol API: esquemas personalizados (dir o handler en el main)
        if (!pendingProtocols_.empty()) AttachProtocolHandlers();

        // webRequest: intercepción de todos los requests (cancelar/redirigir).
        AttachWebRequestHandler();

        // Permisos del WebView: delega en PermissionBroker (la app decide).
        webview_->add_PermissionRequested(
            Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args)
                    -> HRESULT {
                    if (!args) return S_OK;
                    COREWEBVIEW2_PERMISSION_KIND kind =
                        COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;
                    args->get_PermissionKind(&kind);
                    LPWSTR uriRaw = nullptr;
                    args->get_Uri(&uriRaw);
                    const std::string origin = uriRaw ? WideToUtf8(uriRaw) : "";
                    if (uriRaw) CoTaskMemFree(uriRaw);

                    const char* name = "unknown";
                    switch (kind) {
                    case COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION: name = "geolocation"; break;
                    case COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS: name = "notifications"; break;
                    case COREWEBVIEW2_PERMISSION_KIND_MICROPHONE: name = "microphone"; break;
                    case COREWEBVIEW2_PERMISSION_KIND_CAMERA: name = "camera"; break;
                    case COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ: name = "clipboardRead"; break;
                    default: break;
                    }

                    ComPtr<ICoreWebView2Deferral> deferral;
                    args->GetDeferral(&deferral);
                    ComPtr<ICoreWebView2PermissionRequestedEventArgs> hold = args;
                    PermissionBroker::Get().Request(
                        name, origin, [hold, deferral](bool allow) {
                            hold->put_State(allow
                                                ? COREWEBVIEW2_PERMISSION_STATE_ALLOW
                                                : COREWEBVIEW2_PERMISSION_STATE_DENY);
                            if (deferral) deferral->Complete();
                        });
                    return S_OK;
                })
                .Get(),
            nullptr);

        // before-input-event (teclado). No se consume (Handled=false).
        controller_->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [this](ICoreWebView2Controller*,
                       ICoreWebView2AcceleratorKeyPressedEventArgs* a) -> HRESULT {
                    if (!a) return S_OK;
                    COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
                    a->get_KeyEventKind(&kind);
                    UINT vk = 0;
                    a->get_VirtualKey(&vk);
                    const bool down = kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN ||
                                      kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN;
                    std::string json = std::string("{\"type\":\"") +
                                       (down ? "keyDown" : "keyUp") +
                                       "\",\"virtualKey\":" + std::to_string(vk) +
                                       ",\"key\":\"" + KeyName(vk) + "\"}";
                    EmitEvent("beforeInput", json);
                    return S_OK;
                })
                .Get(),
            nullptr);

        // window.open / target=_blank → la app decide (setWindowOpenHandler).
        webview_->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a)
                    -> HRESULT {
                    if (!a || !WindowOpenBroker::Get().Enabled()) return S_OK;
                    LPWSTR uriRaw = nullptr;
                    a->get_Uri(&uriRaw);
                    std::string url = uriRaw ? WideToUtf8(uriRaw) : "";
                    if (uriRaw) CoTaskMemFree(uriRaw);
                    ComPtr<ICoreWebView2Deferral> deferral;
                    a->GetDeferral(&deferral);
                    ComPtr<ICoreWebView2NewWindowRequestedEventArgs> hold = a;
                    WindowOpenBroker::Get().Request(url, [hold, deferral](bool allow) {
                        if (!allow) hold->put_Handled(TRUE);
                        if (deferral) deferral->Complete();
                    });
                    return S_OK;
                })
                .Get(),
            nullptr);

        if (!pendingUrl_.empty()) {
            std::string u = pendingUrl_;
            pendingUrl_.clear();
            LoadURL(u);
        }

        RECT rc;
        GetClientRect(hwnd_, &rc);
        controller_->put_Bounds(rc);
        {
            std::string d = "bounds " + std::to_string(rc.right - rc.left) + "x" +
                            std::to_string(rc.bottom - rc.top);
            log::Info("webview2", d);
        }

        // Diagnóstico de ciclo de vida: sin esto, un fallo del renderer/GPU deja
        // la ventana en blanco y sin ninguna pista en el log.
        webview_->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
                    LPWSTR u = nullptr;
                    if (a && SUCCEEDED(a->get_Uri(&u)) && u) {
                        log::Info("webview2", std::string("nav starting: ") + WideToUtf8(u));
                        CoTaskMemFree(u);
                    }
                    return S_OK;
                }).Get(), nullptr);
        webview_->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
                    BOOL ok = FALSE;
                    COREWEBVIEW2_WEB_ERROR_STATUS st = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                    if (a) { a->get_IsSuccess(&ok); a->get_WebErrorStatus(&st); }
                    log::Info("webview2", std::string("nav completed ok=") +
                        (ok ? "1" : "0") + " status=" +
                        std::to_string(static_cast<int>(st)));
                    return S_OK;
                }).Get(), nullptr);
        webview_->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* a) -> HRESULT {
                    COREWEBVIEW2_PROCESS_FAILED_KIND k =
                        COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
                    if (a) a->get_ProcessFailedKind(&k);
                    log::Error("webview2", "process failed kind=" +
                        std::to_string(static_cast<int>(k)));
                    std::string payload = "{\"name\":\"child-process-gone\",\"payload\":{\"kind\":" +
                                          std::to_string(static_cast<int>(k)) + "}}";
                    ow::ControlServer::Get().BroadcastEvent("app.event", payload);
                    return S_OK;
                }).Get(), nullptr);

    }

} // namespace ow
