// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/TarGz/Helpers.cpp — primitivas de lectura de cabeceras tar.
#include "Internal.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace ow::archive {
namespace archive_detail {

bool IsZeroBlock(const uint8_t* b) {
    for (size_t i = 0; i < kBlock; ++i)
        if (b[i] != 0) return false;
    return true;
}

uint64_t ParseOctal(const uint8_t* p, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = p[i];
        if (c == 0 || c == ' ') break;
        if (c < '0' || c > '7') continue;
        v = v * 8 + (c - '0');
    }
    return v;
}

std::string ParseName(const uint8_t* h) {
    // ustar: name[0..99] + prefix[345..499]
    std::string name(reinterpret_cast<const char*>(h), 100);
    name.resize(::strnlen(name.c_str(), 100));
    if (h[345] != 0) {
        std::string prefix(reinterpret_cast<const char*>(h + 345), 155);
        prefix.resize(::strnlen(prefix.c_str(), 155));
        if (!prefix.empty()) return prefix + "/" + name;
    }
    return name;
}

} // namespace archive_detail
} // namespace ow::archive
