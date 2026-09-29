// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/TarGz.cpp — streaming: inflate por chunks + parser ustar
// incremental (sin cargar todo en RAM).
//
#include "TarGz.hpp"
#include "TarGz/Internal.hpp"

#include "../Core/Log.hpp"

#include <zlib.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string.h>
#include <fstream>
#include <sys/stat.h>

namespace ow::archive {

using namespace archive_detail;

bool ExtractTarGz(const std::filesystem::path& tarGz,
                  const std::filesystem::path& destDir,
                  std::string& error,
                  const std::function<void(const std::string&)>& onEntry) {
#ifdef _WIN32
    std::FILE* f = _wfopen(tarGz.c_str(), L"rb"); // path::c_str() es wchar_t*
#else
    std::FILE* f = std::fopen(tarGz.c_str(), "rb");
#endif
    if (!f) { error = "no se pudo abrir " + tarGz.string(); return false; }

    std::filesystem::create_directories(destDir);
    TarWriter writer(destDir);

    z_stream zs{};
    // 15+16 = gzip automático
    if (inflateInit2(&zs, 15 + 16) != Z_OK) {
        std::fclose(f);
        error = "inflateInit2 falló";
        return false;
    }

    std::array<uint8_t, 64 * 1024> in{};
    std::array<uint8_t, 128 * 1024> out{};

    // Estado del parser tar
    uint8_t hdr[kBlock];
    size_t hdrFill = 0;
    uint64_t dataLeft = 0;
    uint64_t padLeft = 0;
    bool inData = false;
    std::string longName;
    bool pendingLong = false;
    bool sawEnd = false;
    bool archiveEnded = false;
    bool ok = true;

    auto feed = [&](const uint8_t* p, size_t n) -> bool {
        while (n > 0) {
            if (archiveEnded) return true; // padding/trailing garbage tras el fin

            if (!inData) {
                size_t need = kBlock - hdrFill;
                size_t take = need < n ? need : n;
                std::memcpy(hdr + hdrFill, p, take);
                hdrFill += take;
                p += take;
                n -= take;
                if (hdrFill < kBlock) continue;

                hdrFill = 0;
                if (IsZeroBlock(hdr)) {
                    if (sawEnd) {
                        // dos bloques cero seguidos = fin LEGÍTIMO del archivo
                        archiveEnded = true;
                        return true;
                    }
                    sawEnd = true;
                    continue;
                }
                sawEnd = false;

                std::string name = ParseName(hdr);
                char type = static_cast<char>(hdr[156]);
                uint64_t size = ParseOctal(hdr + 124, 12);

                if (type == 'L') {
                    writer.collectingLong_ = true;
                    pendingLong = true;
                }

                if (!writer.Begin(longName, name, type, size, error)) return false;

                uint64_t rounded = (size + kBlock - 1) / kBlock * kBlock;
                if (rounded > 0) {
                    inData = true;
                    dataLeft = size;          // solo los bytes reales al fichero
                    padLeft = rounded - size; // padding a descartar
                } else {
                    std::string outLong;
                    writer.End(error, outLong, onEntry);
                    if (pendingLong) { longName = outLong; pendingLong = false; }
                    else longName.clear();
                }
                continue;
            }

            size_t take = dataLeft < n ? static_cast<size_t>(dataLeft) : n;
            writer.Data(p, take);
            dataLeft -= take;
            p += take;
            n -= take;
            if (dataLeft == 0) {
                // descarta el padding (hasta el bloque de 512) que aún quede
                size_t skip = padLeft < n ? static_cast<size_t>(padLeft) : n;
                padLeft -= skip;
                p += skip;
                n -= skip;
                if (padLeft != 0) continue; // el resto del padding vendrá después
                std::string outLong;
                writer.End(error, outLong, onEntry);
                if (pendingLong) { longName = outLong; pendingLong = false; }
                else longName.clear();
                inData = false;
            }
        }
        return true;
    };

    int ret = Z_OK;
    while (!std::feof(f)) {
        size_t got = std::fread(in.data(), 1, in.size(), f);
        if (got == 0) break;
        zs.next_in = in.data();
        zs.avail_in = static_cast<uInt>(got);
        do {
            zs.next_out = out.data();
            zs.avail_out = static_cast<uInt>(out.size());
            ret = inflate(&zs, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
                error = "inflate error " + std::to_string(ret);
                ok = false;
                break;
            }
            size_t produced = out.size() - zs.avail_out;
            if (produced > 0 && !feed(out.data(), produced)) { ok = false; break; }
        } while (zs.avail_out == 0 && ret != Z_STREAM_END);
        if (!ok) break;
    }

    inflateEnd(&zs);
    std::fclose(f);
    if (ok && !inData && hdrFill == 0) return true;
    if (ok) { error = "tar truncado"; return false; }
    return false;
}

} // namespace ow::archive
