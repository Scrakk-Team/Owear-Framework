// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager/Semver.cpp — comparacion/consulta de rangos semver.
#include "Internal.hpp"

#include <string>

namespace ow {
namespace nodemanager_detail {

int SemverCompare(const std::string& a, const std::string& b) {
    // a/b: "v22.4.0"
    auto parse = [](const std::string& s, int out[3]) {
        int idx = 0, num = 0;
        for (char c : s) {
            if (c == 'v') continue;
            if (c == '.') {
                if (idx < 3) out[idx++] = num;
                num = 0;
            } else if (c >= '0' && c <= '9') {
                num = num * 10 + (c - '0');
            }
        }
        if (idx < 3) out[idx++] = num;
        while (idx < 3) out[idx++] = 0;
    };
    int pa[3] = {}, pb[3] = {};
    parse(a, pa);
    parse(b, pb);
    for (int i = 0; i < 3; ++i)
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    return 0;
}

/// ¿`version` ("v22.12.0") satisface `range` ("latest"|"lts"|"v22"|"22.12.0")?
bool SatisfiesRange(const std::string& version, const std::string& range) {
    if (range.empty() || range == "latest" || range == "lts") return true;
    std::string want = range;
    if (want[0] != 'v') want = "v" + want;
    if (version == want) return true;
    auto dot = want.find('.', 1);
    std::string prefix = dot == std::string::npos ? want : want.substr(0, dot);
    return version.compare(0, prefix.size(), prefix) == 0 &&
           (version.size() == prefix.size() || version[prefix.size()] == '.');
}

} // namespace nodemanager_detail
} // namespace ow
