// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/win/Webview2Backend.hpp — declaracion del backend WebView2.
#pragma once
#include "../IWebviewBackend.hpp"
#include "Internal.hpp"
#include "../../Core/Log.hpp"
#include "../../Bridge/Dispatcher.hpp"
#include "../../Control/ControlServer.hpp"
#include "../../Protocol/ProtocolRegistry.hpp"
#include "../../Session/PermissionBroker.hpp"
#include "../../Session/WebRequestBroker.hpp"
#include "../../Session/WindowOpenBroker.hpp"
#include "ow/Module.h"
#include "ow/detail/minjson.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <wrl/client.h>
#include <wrl/event.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace ow {

using namespace Microsoft::WRL;
using namespace webview2_detail;

struct EnvCreateArgs {
    ICoreWebView2EnvironmentOptions* opts;
    const wchar_t* userDataDir;
    void* self;
};

HRESULT EnvCreateInner(EnvCreateArgs* a);
HRESULT EnvCreateSeh(EnvCreateArgs* a, unsigned long* sehCode);

class Webview2Backend final : public IWebviewBackend {
public:
    bool Create(void* parentNativeWindow, const std::vector<std::string>& args) override {
        hwnd_ = static_cast<HWND>(parentNativeWindow);
        if (!hwnd_) return false;
        try {
            return CreateInner(parentNativeWindow, args);
        } catch (const std::exception& e) {
            log::Error("webview2", std::string("excepción en Create: ") +
                                       e.what());
            return false;
        } catch (...) {
            log::Error("webview2", "excepción desconocida en Create");
            return false;
        }
    }

private:
    bool CreateInner(void* parentNativeWindow, const std::vector<std::string>& args) {
        // COM apartment en el hilo de UI (requisito de WebView2)
        thread_local bool comInit = false;
        if (!comInit) {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            comInit = true;
        }

        auto envOptions = Make<CoreWebView2EnvironmentOptions>();
        partition_ = PartitionFromArgs(args);
        {
            std::wstring joined;
            for (const auto& a : args) {
                if (a.rfind("ow-partition=", 0) == 0) continue; // arg de Owear, no del navegador
                joined += Utf8ToWide(a) + L" ";
            }
            if (!joined.empty())
                envOptions->put_AdditionalBrowserArguments(joined.c_str());
        }
        log::Info("webview2", "opciones listas, user data dir: " +
                                  WideToUtf8(UserDataDir(partition_).c_str()));

        // ¿hay runtime Evergreen instalado? (API de diagnóstico segura)
        LPWSTR ver = nullptr;
        HRESULT hvr = GetAvailableCoreWebView2BrowserVersionString(
            nullptr, &ver);
        log::Info("webview2",
                  "runtime: hr=0x" +
                      std::to_string(static_cast<unsigned long>(hvr)) + " ver=" +
                      (ver ? WideToUtf8(ver) : "(null)"));
        if (ver) CoTaskMemFree(ver);

        std::wstring userDataDir = UserDataDir(partition_);
        ICoreWebView2EnvironmentOptions* rawOpts =
            args.empty() ? nullptr : envOptions.Get();
        EnvCreateArgs a{rawOpts, userDataDir.c_str(), this};
        unsigned long sehCode = 0;
        HRESULT hr = EnvCreateSeh(&a, &sehCode);
        if (sehCode != 0) {
            log::Error("webview2", "SEH dentro de CreateCoreWebView2Environment"
                                   "WithOptions: 0x" +
                                       std::to_string(sehCode));
            return false;
        }
        if (FAILED(hr)) {
            log::Error("webview2", "CreateCoreWebView2EnvironmentWithOptions "
                                   "devolvió 0x" +
                                       std::to_string(
                                           static_cast<unsigned long>(hr)));
            return false;
        }

        // el environment llega por callback (asíncrono); Create devuelve true
        return true;
    }

public:
    void OnEnvironmentReady(ICoreWebView2Environment* env) {
        log::Info("webview2", "environment listo → creando controller");
        environment_ = env;
        HRESULT hr = env->CreateCoreWebView2Controller(
            hwnd_,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Controller* ctrl) -> HRESULT {
                    if (FAILED(result) || !ctrl) {
                        log::Error("webview2", "controller falló hr=0x" +
                            std::to_string(static_cast<unsigned long>(result)));
                        return E_FAIL;
                    }
                    log::Info("webview2", "controller listo");
                    controller_ = ctrl;
                    controller_->get_CoreWebView2(&webview_);
                    OnControllerReady();
                    return S_OK;
                })
                .Get());
        if (FAILED(hr))
            log::Error("webview2", "CreateCoreWebView2Controller devolvió 0x" +
                                       std::to_string(
                                           static_cast<unsigned long>(hr)));
    }

    void OnControllerReady();

    void AttachMessageHandler() {
        webview_->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
                    -> HRESULT {
                    LPWSTR msg = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&msg)) && msg) {
                        if (messageHandler_)
                            messageHandler_(WideToUtf8(msg));
                        CoTaskMemFree(msg);
                    }
                    return S_OK;
                })
                .Get(),
            nullptr);
    }

    void AttachAssetMapping() {
        ComPtr<ICoreWebView2_3> wv3;
        if (SUCCEEDED(webview_.As(&wv3))) {
            wv3->SetVirtualHostNameToFolderMapping(
                L"app.owear", assetRoot_.wstring().c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        }
    }

    void InjectInitScript(const std::string& js) override {
        if (webview_)
            webview_->AddScriptToExecuteOnDocumentCreated(Utf8ToWide(js).c_str(),
                                                          nullptr);
        else
            pendingInitScripts_.push_back(js);
    }

    void SetMessageHandler(WebMessageHandler handler) override {
        messageHandler_ = std::move(handler);
        if (webview_) AttachMessageHandler();
    }

    void LoadURL(const std::string& url) override {
        if (!webview_) {
            pendingUrl_ = url;
            return;
        }
        std::string u = url;
        if (u.rfind("app://", 0) == 0) u = "https://app.owear/" + u.substr(6);
        log::Info("webview2", "navigate → " + u);
        if (controller_) controller_->put_IsVisible(TRUE);
        webview_->Navigate(Utf8ToWide(u).c_str());
    }

    void EvalJS(const std::string& js, EvalCallback cb) override {
        if (!webview_) return;
        // ExecuteScript entrega JSON.stringify(resultado) — perfecto para el bridge
        auto* boxed = new EvalCallback(std::move(cb));
        webview_->ExecuteScript(
            Utf8ToWide(js).c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [boxed](HRESULT error, LPCWSTR resultJson) -> HRESULT {
                    std::unique_ptr<EvalCallback> cb(boxed);
                    std::string result = "null";
                    if (SUCCEEDED(error) && resultJson) result = WideToUtf8(resultJson);
                    if (*cb) (*cb)(result, SUCCEEDED(error));
                    return S_OK;
                })
                .Get());
    }

    void RegisterAssetScheme(const std::string& scheme,
                             const std::filesystem::path& root) override {
        (void)scheme; // Windows usa host virtual fijo app.owear
        assetRoot_ = root;
        if (webview_) AttachAssetMapping();
    }

    /// Esquema gestionado por ProtocolRegistry (dir o handler en el main).
    /// En WebView2 se intercepta con WebResourceRequested (respuesta diferida).
    void RegisterProtocol(const std::string& scheme) override {
        if (scheme.empty()) return;
        if (std::find(pendingProtocols_.begin(), pendingProtocols_.end(), scheme) !=
            pendingProtocols_.end())
            return;
        pendingProtocols_.push_back(scheme);
        if (webview_) AttachProtocolHandlers();
    }

    void Resize(int x, int y, int w, int h) override {
        (void)x;
        (void)y;
        RECT rc{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
        if (controller_) controller_->put_Bounds(rc);
    }

    void* NativeWidget() const override { return hwnd_; }

    void SetBackgroundColor(int r, int g, int b, int a) override {
        if (!controller_) return;
        ComPtr<ICoreWebView2Controller2> c2;
        if (FAILED(controller_->QueryInterface(IID_PPV_ARGS(&c2))) || !c2) return;
        COREWEBVIEW2_COLOR col{static_cast<BYTE>(a), static_cast<BYTE>(r),
                               static_cast<BYTE>(g), static_cast<BYTE>(b)};
        c2->put_DefaultBackgroundColor(col);
    }

    void PrintToPDF(PrintCallback cb) override {
        if (!webview_ || !cb) {
            if (cb) cb(false, {});
            return;
        }
        ComPtr<ICoreWebView2_7> wv7;
        if (FAILED(webview_->QueryInterface(IID_PPV_ARGS(&wv7))) || !wv7) {
            cb(false, {});
            return;
        }
        wchar_t tmp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp);
        std::wstring path =
            std::wstring(tmp) + L"owear-print-" + std::to_wstring(GetTickCount64()) + L".pdf";
        struct Ctx {
            std::wstring path;
            PrintCallback* cb;
        };
        auto* ctx = new Ctx{path, new PrintCallback(std::move(cb))};
        printHandler_ = Callback<ICoreWebView2PrintToPdfCompletedHandler>(
            [ctx](HRESULT error, BOOL success) -> HRESULT {
                std::string pdf;
                if (SUCCEEDED(error) && success) {
                    std::ifstream in(ctx->path, std::ios::binary);
                    if (in) {
                        std::ostringstream ss;
                        ss << in.rdbuf();
                        pdf = ss.str();
                    }
                }
                DeleteFileW(ctx->path.c_str());
                (*ctx->cb)(!pdf.empty(), pdf);
                delete ctx->cb;
                delete ctx;
                return S_OK;
            });
        wv7->PrintToPdf(path.c_str(), nullptr, printHandler_.Get());
    }

    void CapturePage(CaptureCallback cb) override {
        if (!webview_ || !cb) {
            if (cb) cb(false, {});
            return;
        }
        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) || !stream) {
            cb(false, {});
            return;
        }
        struct Ctx {
            IStream* stream;
            CaptureCallback* cb;
        };
        auto* ctx = new Ctx{stream, new CaptureCallback(std::move(cb))};
        captureHandler_ = Callback<ICoreWebView2CapturePreviewCompletedHandler>(
            [ctx](HRESULT error) -> HRESULT {
                std::string png;
                if (SUCCEEDED(error)) {
                    STATSTG st{};
                    ctx->stream->Stat(&st, STATFLAG_NONAME);
                    LARGE_INTEGER z{};
                    z.QuadPart = 0;
                    ctx->stream->Seek(z, STREAM_SEEK_SET, nullptr);
                    png.resize(static_cast<size_t>(st.cbSize.QuadPart));
                    ULONG read = 0;
                    ctx->stream->Read(png.data(), static_cast<ULONG>(png.size()),
                                      &read);
                    png.resize(read);
                }
                ctx->stream->Release();
                log::Info("webview2", std::string("capturePage: done ok=") +
                                         (SUCCEEDED(error) ? "1" : "0") +
                                         " bytes=" + std::to_string(png.size()));
                (*ctx->cb)(SUCCEEDED(error), png);
                delete ctx->cb;
                delete ctx;
                return S_OK;
            });
        webview_->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,
                                 stream, captureHandler_.Get());
        log::Info("webview2", "capturePage: CapturePreview lanzado");
    }

    void SetEventSink(WebviewEventSink sink) override { sink_ = std::move(sink); }
    void EmitEvent(const std::string& name, const std::string& json) {
        if (sink_) sink_(name, json);
    }

    /// Fuerza el `prefers-color-scheme` del contenido (0=auto,1=light,2=dark).
    void SetPreferredColorScheme(int scheme) override {
        if (!webview_) return;
        ComPtr<ICoreWebView2_13> wv13;
        if (FAILED(webview_->QueryInterface(IID_PPV_ARGS(&wv13))) || !wv13) return;
        ComPtr<ICoreWebView2Profile> profile;
        if (FAILED(wv13->get_Profile(&profile)) || !profile) return;
        profile->put_PreferredColorScheme(
            static_cast<COREWEBVIEW2_PREFERRED_COLOR_SCHEME>(scheme));
    }

    static std::string KeyName(UINT vk) {
        if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
        if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
        switch (vk) {
        case VK_RETURN: return "Enter";
        case VK_ESCAPE: return "Escape";
        case VK_TAB: return "Tab";
        case VK_SPACE: return "Space";
        case VK_BACK: return "Backspace";
        case VK_DELETE: return "Delete";
        case VK_UP: return "ArrowUp";
        case VK_DOWN: return "ArrowDown";
        case VK_LEFT: return "ArrowLeft";
        case VK_RIGHT: return "ArrowRight";
        default: return {};
        }
    }

private:
    static std::string ContentTypeFromJson(const std::string& headersJson) {
        auto parsed = ow::json::Parse(headersJson);
        if (parsed.value && parsed.value->IsObject()) {
            for (const auto& [k, v] : parsed.value->AsObject()) {
                if ((k == "content-type" || k == "Content-Type") && v.IsString())
                    return v.AsString();
            }
        }
        return "text/html";
    }

    void AttachProtocolHandlers();
    void AttachWebRequestHandler();
    HWND hwnd_ = nullptr;
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;
    WebMessageHandler messageHandler_;
    WebviewEventSink sink_;
    ComPtr<ICoreWebView2CapturePreviewCompletedHandler> captureHandler_;
    ComPtr<ICoreWebView2PrintToPdfCompletedHandler> printHandler_;
    std::vector<std::string> pendingInitScripts_;
    std::string pendingUrl_;
    std::filesystem::path assetRoot_;
    std::string partition_;
    std::vector<std::string> pendingProtocols_;
    bool resourceHandlerAttached_ = false;
    bool webRequestAttached_ = false;
};

} // namespace ow
