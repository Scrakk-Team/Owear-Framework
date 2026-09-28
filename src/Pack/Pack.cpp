// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Pack/Pack.cpp — lee el payload embebido al final del propio ejecutable.
#include "Pack.hpp"

#include "../Core/Log.hpp"
#include "../Runtime/TarGz.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace ow::pack {

namespace {

constexpr char kMagic[8] = {'O', 'W', 'P', 'K', '1', '\0', '\0', '\0'};
constexpr size_t kFooter = 24; // magic(8) + offset(8) + size(8)

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

/// Lee el footer. true si el binario trae payload.
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

} // namespace

bool HasPayload() {
    uint64_t off = 0, sz = 0;
    return ReadFooter(off, sz);
}

std::string EnsureExtracted() {
    uint64_t offset = 0, size = 0;
    if (!ReadFooter(offset, size)) return {};

    // Carpeta por (offset,size): el mismo binario reutiliza su extracción.
    const std::string key = std::to_string(offset) + ":" + std::to_string(size);
    const std::string dir = CacheRoot() + "/" + std::to_string(Fnv1a(key));
    const std::string marker = dir + "/.owpack-ok";
    std::error_code ec;
    if (std::filesystem::exists(marker, ec)) return dir; // ya extraído

    std::filesystem::create_directories(dir, ec);

    // Vuelca [offset,size) a un .tar.gz temporal (fuera de la carpeta destino).
    const std::string tmp = dir + ".payload.tar.gz";
    {
        std::ifstream in(SelfPath(), std::ios::binary);
        if (!in) return {};
        in.seekg(static_cast<std::streamoff>(offset));
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return {};
        std::vector<char> buf(1 << 16);
        uint64_t left = size;
        while (left > 0) {
            const std::streamsize want = static_cast<std::streamsize>(
                left < buf.size() ? left : buf.size());
            in.read(buf.data(), want);
            const std::streamsize got = in.gcount();
            if (got <= 0) break;
            out.write(buf.data(), got);
            left -= static_cast<uint64_t>(got);
        }
    }

    std::string error;
    if (!archive::ExtractTarGz(tmp, dir, error)) {
        log::Error("pack", "extracción falló: " + error);
        std::error_code e2;
        std::filesystem::remove(tmp, e2);
        return {};
    }
    std::error_code e2;
    std::filesystem::remove(tmp, e2);
    std::ofstream(marker) << "ok";
    log::Info("pack", "payload extraído en " + dir);
    return dir;
}

std::string Manifest() {
    const std::string dir = EnsureExtracted();
    if (dir.empty()) return {};
    std::ifstream f(dir + "/manifest.json", std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

} // namespace ow::pack
