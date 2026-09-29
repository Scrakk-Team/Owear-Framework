// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Webviews/Internal.hpp — helpers de webviews hijas (WebView2).
#pragma once
#include "../Internal.hpp"
#include "../PlatformData.hpp"
#include "ow/detail/minjson.hpp"

#include <cstdint>
#include <string>

namespace ow {
namespace webview_win_detail {

std::wstring ViewUserDataDir();
void EmitViewJson(Window::Impl* impl, const char* name, uint32_t id,
                  const char* key, std::string val);
void ApplyViewBounds(Window::Impl::PlatformData* pd, uint32_t id);
void WireViewEvents(Window::Impl* impl, Window::Impl::PlatformData* pd,
                    uint32_t id);
void CreateViewController(Window::Impl* impl, Window::Impl::PlatformData* pd,
                          uint32_t id);
void EnsureViewEnv(Window::Impl* impl, Window::Impl::PlatformData* pd);

} // namespace webview_win_detail
} // namespace ow
