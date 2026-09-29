// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Bridge/Codec/Internal.hpp — scanner de spans JSON del códec.
#pragma once
#include "ow/Bridge/Codec.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ow::bridge {
namespace codec_detail {

/// Rango [b, e) dentro del texto escaneado.
struct Span {
    size_t b = 0, e = 0;
};

void SkipWs(std::string_view s, size_t& i);
bool ReadString(std::string_view s, size_t& i, std::string& out);
bool SkipValue(std::string_view s, size_t& i);

/// Escanea el nivel superior de un objeto JSON a pares clave→span.
bool ScanTop(std::string_view s, std::vector<std::pair<std::string, Span>>& out);

bool StrOf(std::string_view s, const Span* sp, std::string& out);
uint64_t NumOf(std::string_view s, const Span* sp);

} // namespace codec_detail
} // namespace ow::bridge
