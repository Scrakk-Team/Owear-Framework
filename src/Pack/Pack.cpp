// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Pack/Pack.cpp — lee el payload embebido al final del propio ejecutable.
#include "Pack.hpp"
#include "Internal.hpp"

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

using namespace pack_detail;

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
