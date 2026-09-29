// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/win/Internal.hpp — helpers internos del backend WebView2.
#pragma once
#include "../IWebviewBackend.hpp"
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

#include <string>
#include <string_view>
#include <vector>

namespace ow {
namespace webview2_detail {

/// Conversión UTF-8 <-> UTF-16 (Win32).
std::wstring Utf8ToWide(std::string_view s);
std::string WideToUtf8(const wchar_t* w);

/// Perfil de usuario fuera del dir del exe (puede ser read-only en installs de
/// sistema). `partition` aísla cookies/storage por perfil (session API).
std::wstring UserDataDir(const std::string& partition);

/// Extrae `ow-partition=<nombre>` de los args del WebView.
std::string PartitionFromArgs(const std::vector<std::string>& args);

} // namespace webview2_detail
} // namespace ow
