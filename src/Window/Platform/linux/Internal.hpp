// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Internal.hpp — helpers internos de Window (Linux).
#pragma once
#include "ow/Window.h"
#include "../../Window_p.hpp"
#include "PlatformData.hpp"
#include "ow/detail/minjson.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace ow {

std::string CssHex(const std::string& in);
std::string ContrastHex(const std::string& hex);
std::string ShadeHex(const std::string& hex, double amt);
std::string TrimWs(const std::string& s);
int ParseDecorationRadius(const std::filesystem::path& p);
int ThemeWindowRadius();

bool EdgeFromPoint(double x, double y, int w, int h, GdkWindowEdge& edge);
const char* CursorForEdge(GdkWindowEdge e);
void UpdateOverlayMaxIcon(Window::Impl* impl, bool maximized);
void BuildOverlayBar(Window::Impl* impl);
void DestroyOverlayBar(Window::Impl* impl);
void BuildResizeEdges(Window::Impl* impl);

std::string ViewDataDir(const char* sub);
uint32_t ViewIdOf(GtkWidget* view);
void EmitViewEvent(Window::Impl* impl, const char* name, std::string json);
void OnViewLoadChanged(WebKitWebView* view, WebKitLoadEvent ev, gpointer ud);
void OnViewUriChanged(GObject* obj, GParamSpec* ps, gpointer ud);
void OnViewTitleChanged(GObject* obj, GParamSpec* ps, gpointer ud);
gboolean OnViewLoadFailed(WebKitWebView* v, WebKitLoadEvent e, gchar* uri, GError* err, gpointer ud);
gboolean OnViewButtonPress(GtkWidget* w, GdkEventButton* e, gpointer ud);

} // namespace ow
