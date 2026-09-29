// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Pack/Helpers.cpp — helpers del payload single-binary (footer, cache).
#include "Internal.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace ow::pack {
namespace pack_detail {

std::string SelfPath() {
#ifdef _WIN32
    wchar_t wbuf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, wbuf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    char buf[MAX_PATH * 4] = {0};
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, sizeof(buf) - 1, nullptr, nullptr);
    return buf;
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return buf;
#endif
}
bool ReadFooter(uint64_t& offset, uint64_t& size) {
    const std::string self = SelfPath();
    if (self.empty()) return false;
    std::ifstream f(self, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff end = f.tellg();
    if (end < static_cast<std::streamoff>(kFooter)) return false;
    f.seekg(end - static_cast<std::streamoff>(kFooter));
    char buf[kFooter];
    f.read(buf, kFooter);
    if (!f || std::memcmp(buf, kMagic, 8) != 0) return false;
    std::memcpy(&offset, buf + 8, 8);
    std::memcpy(&size, buf + 16, 8);
    return size > 0 && offset > 0 && offset + size <= static_cast<uint64_t>(end);
}
uint64_t Fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}
std::string CacheRoot() {
    std::string base;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
        base = xdg;
    else if (const char* home = std::getenv("HOME"); home && *home)
        base = std::string(home) + "/.cache";
#ifdef _WIN32
    if (const char* la = std::getenv("LOCALAPPDATA"); la && *la)
        base = std::string(la) + "\\Cache";
#endif
    if (base.empty()) base = "/tmp";
    std::string app = "owear";
    if (const char* id = std::getenv("OW_APP_ID"); id && *id) app = id;
    else if (const char* name = std::getenv("OW_APP_NAME"); name && *name) app = name;
    return base + "/owear/" + app + "/pack";
}

} // namespace pack_detail
} // namespace ow::pack
