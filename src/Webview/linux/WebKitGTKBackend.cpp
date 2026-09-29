// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/linux/WebKitGTKBackend.cpp — backend WebKitGTK 4.1.
// Todo en main thread (GTK).
//
// Schemes registrados por instancia:
//   app://    → archivos del directorio de assets (dist/)
//   ow-shm:// → regiones de memoria compartida SIN copia (F3)
//   ow-sync://→ canal síncrono invoke (XHR bloqueante, escape hatch F3)
//
#include "../IWebviewBackend.hpp"
#include "Internal.hpp"
#include "../../Bridge/Dispatcher.hpp"
#include "../../Bridge/Shm.hpp"
#include "../../Control/ControlServer.hpp"
#include "../../Protocol/ProtocolRegistry.hpp"
#include "../../Session/PermissionBroker.hpp"
#include "../../Session/WebRequestBroker.hpp"
#include "../../Session/WindowOpenBroker.hpp"
#include "ow/Bridge/Codec.h"
#include "../../Core/Log.hpp"
#include "ow/Base64.h"
#include "ow/detail/minjson.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <cairo-pdf.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

#include "WebKitGTKBackend.hpp"

namespace ow {

std::unique_ptr<IWebviewBackend> CreateWebviewBackend() {
    return std::make_unique<WebKitGTKBackend>();
}

} // namespace ow
