// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/win/Webview2Backend.cpp — backend WebView2 (Edge Chromium).
//
// Requiere: Microsoft.Web.WebView2 SDK (headers + WebView2Loader).
//   - NuGet: Microsoft.Web.WebView2 → include/ + build/native/WebView2Loader
//   - CMake (F-windows): target_link_libraries ... WebView2Loader
//
// Assets locales: SetVirtualHostNameToFolderMapping("app.owear", root)
//   → la app carga https://app.owear/index.html (origen https real, sin CORS).
//
// VERIFICAR-EN-WINDOWS: primer build del SDK y rutas del loader.
//
#include "../IWebviewBackend.hpp"
#include "../../Core/Log.hpp"
#include "../../Control/ControlServer.hpp"
#include "../../Protocol/ProtocolRegistry.hpp"
#include "../../Session/PermissionBroker.hpp"
#include "../../Session/WebRequestBroker.hpp"
#include "../../Session/WindowOpenBroker.hpp"
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
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace ow {

using namespace Microsoft::WRL;

namespace {

std::wstring Utf8ToWide(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// Perfil de usuario fuera del dir del exe (puede ser read-only en installs
// de sistema) — patrón GetUserDataDir de ole/browser_host.
// `partition` aísla cookies/storage por perfil (session API).
std::wstring UserDataDir(const std::string& partition) {
    PWSTR local = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                       &local))) {
        dir = std::wstring(local) + L"\\owear\\WebView2";
        CoTaskMemFree(local);
    } else {
        dir = L".\\owear-webview2";
    }
    if (!partition.empty()) {
        std::string safe = partition;
        for (char& c : safe)
            if (c == ':' || c == '/' || c == '\\' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        dir += L"\\" + Utf8ToWide(safe);
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec); // no-lanzante
    return dir;
}

/// Extrae `ow-partition=<nombre>` de los args del WebView.
std::string PartitionFromArgs(const std::vector<std::string>& args) {
    static const std::string kPrefix = "ow-partition=";
    for (const auto& a : args) {
        if (a.rfind(kPrefix, 0) == 0 && a.size() > kPrefix.size())
            return a.substr(kPrefix.size());
    }
    return {};
}

// Envoltorio SEH: CreateCoreWebView2EnvironmentWithOptions puede morir con
// fail-fast (no capturable por try/catch ni por el filtro global). La llamada
// real vive en EnvCreateInner (puede usar objetos C++); la función SEH solo
// delega (en su frame no hay nada destructible — requisito /EHsc).
} // namespace (anónimo)

// file-scope: la definición de EnvCreateInner vive al final del archivo,
// fuera del namespace anónimo (el LNK2019 vino del desajuste).
struct EnvCreateArgs {
    ICoreWebView2EnvironmentOptions* opts;
    const wchar_t* userDataDir;
    void* self;
};

HRESULT EnvCreateInner(EnvCreateArgs* a);

HRESULT EnvCreateSeh(EnvCreateArgs* a, unsigned long* sehCode) {
    __try {
        return EnvCreateInner(a);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *sehCode = static_cast<unsigned long>(GetExceptionCode());
        return E_FAIL;
    }
}

namespace {

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

    void OnControllerReady() {
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

    void AttachProtocolHandlers() {
        if (!webview_) return;
        for (const auto& s : pendingProtocols_)
            webview_->AddWebResourceRequestedFilter(
                Utf8ToWide(s + "://*").c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        if (resourceHandlerAttached_) return;
        resourceHandlerAttached_ = true;

        webview_->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args)
                    -> HRESULT {
                    if (!args) return S_OK;
                    ComPtr<ICoreWebView2WebResourceRequest> req;
                    if (FAILED(args->get_Request(&req)) || !req) return S_OK;
                    LPWSTR uriRaw = nullptr;
                    if (FAILED(req->get_Uri(&uriRaw)) || !uriRaw) return S_OK;
                    std::string uri = WideToUtf8(uriRaw);
                    CoTaskMemFree(uriRaw);

                    const auto pos = uri.find("://");
                    if (pos == std::string::npos) return S_OK;
                    const std::string scheme = uri.substr(0, pos);
                    if (!ProtocolRegistry::Get().Has(scheme)) return S_OK;

                    ComPtr<ICoreWebView2Deferral> deferral;
                    if (FAILED(args->GetDeferral(&deferral)) || !deferral) return S_OK;
                    ComPtr<ICoreWebView2WebResourceRequestedEventArgs> hold = args;
                    ComPtr<ICoreWebView2Environment> env = environment_;

                    ProtocolRegistry::Get().Dispatch(
                        scheme, uri, "GET", "{}", "",
                        [this, hold, deferral, env](ProtocolRegistry::Response r) {
                            ComPtr<IStream> stream;
                            if (!r.body.empty()) {
                                HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, r.body.size());
                                if (h) {
                                    if (void* p = GlobalLock(h)) {
                                        std::memcpy(p, r.body.data(), r.body.size());
                                        GlobalUnlock(h);
                                    }
                                    CreateStreamOnHGlobal(h, TRUE, &stream);
                                }
                            }
                            const std::string ct = ContentTypeFromJson(r.headersJson);
                            const std::wstring headers =
                                L"Content-Type: " + Utf8ToWide(ct) +
                                L"\r\nAccess-Control-Allow-Origin: *\r\n";
                            ComPtr<ICoreWebView2WebResourceResponse> resp;
                            if (env)
                                env->CreateWebResourceResponse(stream.Get(), r.status, L"OK",
                                                               headers.c_str(), &resp);
                            if (resp) hold->put_Response(resp.Get());
                            deferral->Complete();
                        });
                    return S_OK;
                })
                .Get(),
            nullptr);
    }

    void AttachWebRequestHandler() {
        if (!webview_ || webRequestAttached_) return;
        webRequestAttached_ = true;
        webview_->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        webview_->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args)
                    -> HRESULT {
                    if (!args || !WebRequestBroker::Get().Enabled()) return S_OK;
                    ComPtr<ICoreWebView2WebResourceRequest> req;
                    if (FAILED(args->get_Request(&req)) || !req) return S_OK;
                    LPWSTR uriRaw = nullptr;
                    if (FAILED(req->get_Uri(&uriRaw)) || !uriRaw) return S_OK;
                    std::string uri = WideToUtf8(uriRaw);
                    CoTaskMemFree(uriRaw);

                    const auto pos = uri.find("://");
                    const std::string scheme =
                        pos == std::string::npos ? "" : uri.substr(0, pos);
                    // Los esquemas de `protocol` los gestiona el otro handler.
                    if (ProtocolRegistry::Get().Has(scheme)) return S_OK;
                    if (!WebRequestBroker::Get().Matches(uri)) return S_OK;

                    ComPtr<ICoreWebView2Deferral> deferral;
                    if (FAILED(args->GetDeferral(&deferral)) || !deferral) return S_OK;
                    ComPtr<ICoreWebView2WebResourceRequestedEventArgs> hold = args;
                    ComPtr<ICoreWebView2Environment> env = environment_;
                    WebRequestBroker::Get().BeforeRequest(
                        uri, "GET", "{}",
                        [env, hold, deferral](WebRequestBroker::Action a) {
                            if (a.cancel) {
                                ComPtr<ICoreWebView2WebResourceResponse> resp;
                                if (env)
                                    env->CreateWebResourceResponse(nullptr, 403,
                                                                   L"Forbidden", L"",
                                                                   &resp);
                                if (resp) hold->put_Response(resp.Get());
                            } else if (!a.redirectUrl.empty()) {
                                const std::wstring headers =
                                    L"Location: " + Utf8ToWide(a.redirectUrl) + L"\r\n";
                                ComPtr<ICoreWebView2WebResourceResponse> resp;
                                if (env)
                                    env->CreateWebResourceResponse(nullptr, 302, L"Found",
                                                                   headers.c_str(), &resp);
                                if (resp) hold->put_Response(resp.Get());
                            }
                            deferral->Complete();
                        });
                    return S_OK;
                })
                .Get(),
            nullptr);
    }

    HWND hwnd_ = nullptr;
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;
    WebMessageHandler messageHandler_;
    WebviewEventSink sink_;
    ComPtr<ICoreWebView2CapturePreviewCompletedHandler> captureHandler_;
    std::vector<std::string> pendingInitScripts_;
    std::string pendingUrl_;
    std::filesystem::path assetRoot_;
    std::string partition_;
    std::vector<std::string> pendingProtocols_;
    bool resourceHandlerAttached_ = false;
    bool webRequestAttached_ = false;
};

} // namespace

// Definición fuera del namespace anónimo: EnvCreateSeh (arriba, con __try)
// la forward-declara. El callback captura `this` vía el puntero del struct.
HRESULT EnvCreateInner(EnvCreateArgs* a) {
    auto* self = static_cast<Webview2Backend*>(a->self);
    return CreateCoreWebView2EnvironmentWithOptions(
        nullptr, a->userDataDir, a->opts,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [self](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) {
                    log::Error("webview2", "environment falló");
                    return E_FAIL;
                }
                self->OnEnvironmentReady(env);
                return S_OK;
            })
            .Get());
}

std::unique_ptr<IWebviewBackend> CreateWebviewBackend() {
    return std::make_unique<Webview2Backend>();
}

} // namespace ow
