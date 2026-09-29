// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/Http/Url.cpp — parseo de URL y cabeceras de respuesta.
#include "Internal.hpp"

#include <cctype>
#include <map>
#include <sstream>
#include <string>

namespace ow::http {
namespace detail {

bool ParseUrl(const std::string& url, Url& out, std::string& error) {
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        error = "URL sin scheme";
        return false;
    }
    out.tls = url.compare(0, schemeEnd, "https") == 0;
    if (!out.tls && url.compare(0, schemeEnd, "http") != 0) {
        error = "solo se soporta http(s)";
        return false;
    }
    if (!out.tls) out.port = "80";
    std::string rest = url.substr(schemeEnd + 3);
    auto pathStart = rest.find('/');
    std::string authority =
        pathStart == std::string::npos ? rest : rest.substr(0, pathStart);
    out.path = pathStart == std::string::npos ? "/" : rest.substr(pathStart);
    auto colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']') == std::string::npos) {
        out.host = authority.substr(0, colon);
        out.port = authority.substr(colon + 1);
    } else {
        out.host = authority;
    }
    if (out.host.empty()) { error = "URL sin host"; return false; }
    return true;
}
bool ParseHeaders(const std::string& head, Response& r) {
    size_t lineEnd = head.find("\r\n");
    if (lineEnd == std::string::npos || head.size() < 12) return false;
    std::istringstream ss(head.substr(9, 3));
    ss >> r.status;
    size_t pos = lineEnd + 2;
    while (pos < head.size()) {
        size_t eol = head.find("\r\n", pos);
        if (eol == std::string::npos) break;
        std::string line = head.substr(pos, eol - pos);
        pos = eol + 2;
        if (line.empty()) break;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        for (auto& c : key) c = static_cast<char>(::tolower(c));
        size_t vstart = colon + 1;
        while (vstart < line.size() && line[vstart] == ' ') ++vstart;
        r.headers[key] = line.substr(vstart);
    }
    return r.status > 0;
}

} // namespace detail
} // namespace ow::http
