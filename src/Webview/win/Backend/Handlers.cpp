// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/win/Backend/Handlers.cpp — esquemas custom y web-request.
#include "../Webview2Backend.hpp"
#include "../../../Session/Charter.hpp"

namespace ow {

void Webview2Backend::AttachProtocolHandlers() {
        if (!webview_) return;
        for (const auto& s : pendingProtocols_)
            webview_->AddWebResourceRequestedFilter(
                Utf8ToWide(s + "://*").c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        // RPC por esquema de los módulos nativos (args en el body POST).
        webview_->AddWebResourceRequestedFilter(L"ow-rpc://*",
                                                COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
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

                    // ── ow-rpc://call/<mod>/<fn>?w=<id> — invoke por esquema:
                    //    args en el body POST, respuesta en el body. Sin
                    //    postMessage de request ni eval de respuesta.
                    if (scheme == "ow-rpc") {
                        std::string body;
                        {
                            ComPtr<IStream> content;
                            if (SUCCEEDED(req->get_Content(&content)) && content) {
                                char buf[65536];
                                ULONG n = 0;
                                while (SUCCEEDED(content->Read(buf, sizeof(buf), &n)) && n > 0)
                                    body.append(buf, n);
                            }
                        }
                        std::string rest = uri, query, mod, fn;
                        const std::string prefix = "ow-rpc://call/";
                        if (rest.rfind(prefix, 0) == 0) {
                            rest = rest.substr(prefix.size());
                            const auto qpos = rest.find('?');
                            if (qpos != std::string::npos) {
                                query = rest.substr(qpos + 1);
                                rest = rest.substr(0, qpos);
                            }
                            const auto slash = rest.find('/');
                            mod = slash == std::string::npos ? rest : rest.substr(0, slash);
                            fn = slash == std::string::npos ? std::string()
                                                            : rest.substr(slash + 1);
                        }
                        WindowId wid = 0;
                        if (const auto wp = query.find("w="); wp != std::string::npos)
                            wid = static_cast<WindowId>(
                                std::strtoul(query.c_str() + wp + 2, nullptr, 10));
                        std::string argsJson = body.empty() ? std::string("[]") : body;
                        std::string out;
                        if (mod.empty() || fn.empty()) {
                            out = "{\"ok\":false,\"r\":{\"message\":\"ow-rpc: ruta "
                                  "inválida\"}}";
                        } else if (RendererCallAllowed(wid, mod, fn, out)) {
                            ow_request_t oreq{};
                            oreq.json = argsJson.c_str();
                            oreq.json_len = static_cast<uint32_t>(argsJson.size());
                            ow_response_t ores{};
                            Dispatcher::Get().Execute(wid, mod, fn, &oreq, &ores);
                            if (ores.status != 0) {
                                json::Object e;
                                e.emplace_back(
                                    "message",
                                    json::Value(std::string(ores.error ? ores.error : "")));
                                out = "{\"ok\":false,\"r\":" +
                                      json::Value(std::move(e)).Serialize() + "}";
                            } else {
                                out = "{\"ok\":true,\"r\":" +
                                      std::string(ores.json, ores.json_len) + "}";
                            }
                        }
                        ComPtr<IStream> stream;
                        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, out.size());
                        if (h) {
                            if (void* p = GlobalLock(h)) {
                                std::memcpy(p, out.data(), out.size());
                                GlobalUnlock(h);
                            }
                            CreateStreamOnHGlobal(h, TRUE, &stream);
                        }
                        ComPtr<ICoreWebView2WebResourceResponse> resp;
                        if (environment_)
                            environment_->CreateWebResourceResponse(
                                stream.Get(), 200, L"OK",
                                L"Content-Type: application/json\r\n"
                                L"Access-Control-Allow-Origin: *\r\n",
                                &resp);
                        if (resp) args->put_Response(resp.Get());
                        return S_OK;
                    }

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

void Webview2Backend::AttachWebRequestHandler() {
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
                    if (scheme == "ow-rpc") return S_OK;
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

} // namespace ow
