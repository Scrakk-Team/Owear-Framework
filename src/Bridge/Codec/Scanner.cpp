// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Bridge/Codec/Scanner.cpp — scanner de spans JSON (sin asignar strings).
#include "Internal.hpp"

#include "ow/Base64.h"
#include "ow/detail/minjson.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ow::bridge {
namespace codec_detail {


void SkipWs(std::string_view s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

/// Lee un string JSON (s[i]=='"') y devuelve su contenido desescapado.
bool ReadString(std::string_view s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c != '\\') {
            out += c;
            continue;
        }
        if (i >= s.size()) return false;
        char e = s[i++];
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':
                // claves/valores cortos: conservamos el escape tal cual
                if (i + 4 > s.size()) return false;
                out += "\\u";
                out.append(s.substr(i, 4));
                i += 4;
                break;
            default: out += e; break; // \" \\ \/
        }
    }
    return false;
}

/// Salta un valor JSON completo desde i (sin construirlo).
bool SkipValue(std::string_view s, size_t& i) {
    SkipWs(s, i);
    if (i >= s.size()) return false;
    const char c = s[i];
    if (c == '"') {
        std::string tmp;
        return ReadString(s, i, tmp);
    }
    if (c == '{' || c == '[') {
        const char open = c, close = (c == '{') ? '}' : ']';
        int depth = 0;
        while (i < s.size()) {
            const char d = s[i];
            if (d == '"') {
                std::string tmp;
                if (!ReadString(s, i, tmp)) return false;
                continue;
            }
            if (d == open) ++depth;
            else if (d == close) {
                --depth;
                if (depth == 0) {
                    ++i;
                    return true;
                }
            }
            ++i;
        }
        return false;
    }
    while (i < s.size()) {
        const char d = s[i];
        if (d == ',' || d == '}' || d == ']' || d == ' ' || d == '\t' || d == '\n' ||
            d == '\r')
            break;
        ++i;
    }
    return true;
}

/// Escanea el objeto raíz: clave → span del valor (crudo, sin construirlo).
bool ScanTop(std::string_view s, std::vector<std::pair<std::string, Span>>& out) {
    size_t i = 0;
    SkipWs(s, i);
    if (i >= s.size() || s[i] != '{') return false;
    ++i;
    for (;;) {
        SkipWs(s, i);
        if (i < s.size() && s[i] == '}') return true;
        if (i >= s.size() || s[i] != '"') return false;
        std::string key;
        if (!ReadString(s, i, key)) return false;
        SkipWs(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        SkipWs(s, i);
        const size_t vb = i;
        if (!SkipValue(s, i)) return false;
        out.emplace_back(std::move(key), Span{vb, i});
        SkipWs(s, i);
        if (i < s.size() && s[i] == ',') {
            ++i;
            continue;
        }
        if (i < s.size() && s[i] == '}') return true;
        return false;
    }
}

bool StrOf(std::string_view s, const Span* sp, std::string& out) {
    if (!sp) return false;
    size_t i = sp->b;
    return ReadString(s, i, out);
}

uint64_t NumOf(std::string_view s, const Span* sp) {
    if (!sp) return 0;
    uint64_t v = 0;
    for (size_t i = sp->b; i < sp->e; ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') break;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    return v;
}


} // namespace codec_detail
} // namespace ow::bridge
