// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Internal.hpp — helpers internos de Window (Linux).
#pragma once

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

} // namespace ow
