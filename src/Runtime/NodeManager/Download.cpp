// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager/Download.cpp — descarga/verificacion/extraccion del runtime.
#include "../NodeManager.hpp"
#include "Internal.hpp"
#include "../../Core/Log.hpp"
#include "../Http.hpp"
#include "../Sha256.hpp"
#include "../TarGz.hpp"
#include "ow/detail/minjson.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ow {

namespace fs = std::filesystem;

Result<fs::path> NodeManager::Ensure(const std::string& range) {
    // 1. cache directa si el range es exacto y ya existe
    std::vector<Release> list;
    std::string error;
    if (!FetchIndex(list, error)) {
        // sin red → usa cache existente aunque no coincida
        if (auto cached = FindCached()) {
            log::Warn("node", "sin red; usando runtime cacheado");
            return Result<fs::path>::Ok(*cached);
        }
        return Result<fs::path>::Err(error);
    }

    const Release* rel = Pick(list, range);
    if (!rel) return Result<fs::path>::Err("ninguna release satisface '" + range + "'");

    std::string plat = PlatformTag();
    std::string arch = ArchTag();
    std::string dirName = "node-" + rel->version + "-" + plat + "-" + arch;
    std::string fileName = dirName + (plat == "win" ? ".zip" : ".tar.gz");
    fs::path target = CacheRoot() / dirName;

#ifndef _WIN32
    fs::path nodeBin = target / "bin" / "node";
#else
    fs::path nodeBin = target / "node.exe";
#endif

    std::error_code ec;
    if (fs::exists(nodeBin, ec)) return Result<fs::path>::Ok(nodeBin);

    // 2. descarga tarball + SHASUMS256.txt
    std::string base = "https://nodejs.org/dist/" + rel->version + "/";
    fs::path tmpDir = CacheRoot() / "tmp";
    fs::create_directories(tmpDir, ec);
    fs::path tgzPath = tmpDir / fileName;
    fs::path shaPath = tmpDir / "SHASUMS256.txt";

    log::Info("node", "descargando " + fileName);
    if (!http::DownloadToFile(base + fileName, tgzPath, error))
        return Result<fs::path>::Err(error);

    // 3. verificación SHA256
    std::string shasums;
    if (!http::DownloadToString(base + "SHASUMS256.txt", shasums, error))
        return Result<fs::path>::Err(error);

    crypto::Sha256 h;
    {
        std::ifstream f(tgzPath, std::ios::binary);
        char buf[65536];
        while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
            h.Update(reinterpret_cast<uint8_t*>(buf), static_cast<size_t>(f.gcount()));
    }
    std::string actual = h.Hex();

    std::istringstream ss(shasums);
    std::string line;
    bool verified = false;
    while (std::getline(ss, line)) {
        auto pos = line.find(fileName);
        if (pos != std::string::npos && line.size() >= 64) {
            std::string expected = line.substr(0, 64);
            verified = (expected == actual);
            break;
        }
    }
    if (!verified) {
        fs::remove(tgzPath, ec);
        return Result<fs::path>::Err("SHA256 no coincide para " + fileName +
                                     " (esperaba suma registrada)");
    }
    log::Info("node", "sha256 verificado ✓");

    // 4. extracción
    if (plat == "win") {
        return Result<fs::path>::Err(
            "extracción .zip pendiente en Windows (F3); usa Node del sistema");
    }
    if (!archive::ExtractTarGz(tgzPath, CacheRoot(), error) || error.empty() == false) {
        fs::remove_all(target, ec);
        return Result<fs::path>::Err(error.empty() ? "extracción fallida" : error);
    }
    if (!fs::exists(nodeBin, ec)) {
        fs::remove_all(target, ec);
        return Result<fs::path>::Err("extracción incompleta: falta " + nodeBin.string());
    }
    fs::permissions(nodeBin,
                    fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                        fs::perms::others_read | fs::perms::others_exec,
                    ec);
    fs::remove(tgzPath, ec);
    log::Info("node", "runtime listo: " + nodeBin.string());
    return Result<fs::path>::Ok(nodeBin);
}

} // namespace ow
