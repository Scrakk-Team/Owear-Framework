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

#include "Webview2Backend.hpp"


namespace ow {

using namespace Microsoft::WRL;


// Envoltorio SEH: CreateCoreWebView2EnvironmentWithOptions puede morir con
// fail-fast (no capturable por try/catch ni por el filtro global). La llamada
// real vive en EnvCreateInner (puede usar objetos C++); la función SEH solo
// delega (en su frame no hay nada destructible — requisito /EHsc).

// file-scope: la definición de EnvCreateInner vive al final del archivo,
// fuera del namespace anónimo (el LNK2019 vino del desajuste).

HRESULT EnvCreateSeh(EnvCreateArgs* a, unsigned long* sehCode) {
    __try {
        return EnvCreateInner(a);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *sehCode = static_cast<unsigned long>(GetExceptionCode());
        return E_FAIL;
    }
}

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
