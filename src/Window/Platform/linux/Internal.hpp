// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Internal.hpp — helpers internos de Window (Linux).
#pragma once

#include "../../Window_p.hpp"
#include "ow/detail/minjson.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace ow {

/// "#RGB"/"#RRGGBB"/"#RRGGBBAA" -> "#rrggbb" ("" si transparente/invalido).
std::string CssHex(const std::string& in);
/// "#rrggbb" -> "#000000"/"#ffffff" por luminancia.
std::string ContrastHex(const std::string& hex);
/// Aclara (amt>0) u oscurece (amt<0) un "#rrggbb".
std::string ShadeHex(const std::string& hex, double amt);
std::string TrimWs(const std::string& s);
/// Radio del `decoration { border-radius }` del tema (-1 si no).
int ParseDecorationRadius(const std::filesystem::path& p);
/// Radio de esquina de la ventana segun el tema (fallback 10).
int ThemeWindowRadius();

std::string ViewDataDir(const char* sub);
uint32_t ViewIdOf(GtkWidget* view);
void EmitViewEvent(Window::Impl* impl, const char* name, std::string json);
void OnViewLoadChanged(WebKitWebView* view, WebKitLoadEvent ev, gpointer ud);
void OnViewUriChanged(GObject* obj, GParamSpec* ps, gpointer ud);
void OnViewTitleChanged(GObject* obj, GParamSpec* ps, gpointer ud);
gboolean OnViewLoadFailed(WebKitWebView* v, WebKitLoadEvent e, gchar* uri, GError* err, gpointer ud);
gboolean OnViewButtonPress(GtkWidget* w, GdkEventButton* e, gpointer ud);

} // namespace ow
