// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Internal.hpp — helpers internos de Window (Windows).
#pragma once
#include "ow/Window.h"
#include "../../Window_p.hpp"
#include "PlatformData.hpp"
#include "ow/detail/minjson.hpp"

#include <cstdint>
#include <string>

namespace ow {

/// Clase de la ventana de botones del titleBarOverlay.
constexpr wchar_t kCaptionClass[] = L"OwearCaptionButtons";

void PositionCaptionBar(Window::Impl::PlatformData* pd);
void DrawCaptionBar(HWND hwnd, Window::Impl::PlatformData* pd);
void RegisterCaptionClassOnce();
COLORREF ParseHexColorRef(const std::string& in, COLORREF def);

} // namespace ow
