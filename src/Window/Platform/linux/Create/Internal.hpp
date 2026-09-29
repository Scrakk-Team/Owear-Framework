// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Create/Internal.hpp — fases de PCreate (GTK/WebKitGTK).
#pragma once
#include "../Internal.hpp"
#include "../PlatformData.hpp"

namespace ow {

void ApplyFramelessChrome(Window::Impl* impl, bool frameless);
void CreateViewContext(Window::Impl* impl);
void ApplyWindowOptions(Window::Impl* impl);
void WireWindowSignals(Window::Impl* impl);
bool CreateMainWebview(Window::Impl* impl, bool frameless);
void RegisterWindowSchemes(Window::Impl* impl);

GdkFilterReturn ResizeEventFilter(GdkXEvent*, GdkEvent*, gpointer);
bool DisplayIsWayland();
gboolean OnWindowButtonPress(GtkWidget*, GdkEventButton*, gpointer);

} // namespace ow
