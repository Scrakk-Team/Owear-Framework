// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Pack/Internal.hpp — helpers internos del payload single-binary.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace ow::pack {
namespace pack_detail {

constexpr char kMagic[8] = {'O', 'W', 'P', 'K', '1', '\0', '\0', '\0'};
constexpr size_t kFooter = 24; // magic(8) + offset(8) + size(8)

/// Ruta absoluta del binario actual.
std::string SelfPath();

/// Lee el footer. true si el binario trae payload.
bool ReadFooter(uint64_t& offset, uint64_t& size);

uint64_t Fnv1a(const std::string& s);

/// Directorio de caché de extracción (XDG / LOCALAPPDATA).
std::string CacheRoot();

} // namespace pack_detail
} // namespace ow::pack
