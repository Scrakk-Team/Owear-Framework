// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager/Resolve.cpp — resolucion del runtime (sistema primero).
#include "Internal.hpp"
#include "../../Core/Log.hpp"
#include "../Http.hpp"
#include "../Sha256.hpp"
#include "../TarGz.hpp"
#include "ow/detail/minjson.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace ow {

using namespace nodemanager_detail;

namespace fs = std::filesystem;

// ── resolución del runtime (sistema primero) ────────────────────────────────

std::string NodeManager::MinVersion() {
    // Alineado con `engines` del SDK. Si esto sube, sube también package.json.
    return "v20.0.0";
}

std::string NodeManager::QueryVersion(const fs::path& bin) {
    std::string cmd = "\"" + bin.string() + "\" --version";
#ifndef _WIN32
    cmd += " 2>/dev/null";
#endif
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return {};
    char buf[256];
    std::string out;
    while (std::fgets(buf, sizeof(buf), pipe)) out += buf;
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    // Trim de CR/LF/espacios finales. Códigos numéricos a propósito: el
    // literal escapado se corrompe con facilidad al editar el archivo.
    while (!out.empty() &&
           (out.back() == 10 || out.back() == 13 || out.back() == 32))
        out.pop_back();
    // Sólo aceptamos algo con forma de versión de node ("v22.12.0").
    if (out.size() < 2 || out[0] != 'v') return {};
    return out;
}



std::optional<fs::path> NodeManager::FindExplicit() {
    const char* env = std::getenv("OW_NODE_BIN");
    if (!env || !*env) return std::nullopt;
    fs::path p(env);
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        log::Warn("node", std::string("OW_NODE_BIN no existe: ") + env);
        return std::nullopt;
    }
    return p;
}

std::optional<fs::path> NodeManager::FindSystem() {
#ifdef _WIN32
    const char* exeName = "node.exe";
    const char sep = ';';
#else
    const char* exeName = "node";
    const char sep = ':';
#endif
    // Un candidato sólo cuenta si de verdad ejecuta (`--version` responde).
    auto usable = [](const fs::path& p) {
        std::error_code ec;
        return fs::exists(p, ec) && fs::is_regular_file(p, ec) &&
               !NodeManager::QueryVersion(p).empty();
    };

    if (const char* pathEnv = std::getenv("PATH"); pathEnv && *pathEnv) {
        std::string paths(pathEnv);
        size_t start = 0;
        for (;;) {
            size_t end = paths.find(sep, start);
            std::string dir =
                paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!dir.empty() && usable(fs::path(dir) / exeName)) return fs::path(dir) / exeName;
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

#ifdef _WIN32
    // El instalador oficial de Windows no siempre entra en el PATH.
    if (const char* pf = std::getenv("ProgramFiles"); pf && *pf) {
        fs::path cand = fs::path(pf) / "nodejs" / "node.exe";
        if (usable(cand)) return cand;
    }
#else
    for (const char* p : {"/usr/local/bin/node", "/usr/bin/node", "/bin/node"}) {
        fs::path cand(p);
        if (usable(cand)) return cand;
    }
#endif
    return std::nullopt;
}

Result<NodeManager::Runtime> NodeManager::Resolve(const std::string& range) {
    const std::string min = MinVersion();

    auto accept = [&](const fs::path& bin, const char* source) -> std::optional<Runtime> {
        std::string ver = QueryVersion(bin);
        if (ver.empty()) {
            log::Warn("node", "no se pudo ejecutar como node: " + bin.string());
            return std::nullopt;
        }
        if (SemverCompare(ver, min) < 0) {
            log::Warn("node", ver + " en " + bin.string() + " es anterior al mínimo " + min +
                                  "; se ignora");
            return std::nullopt;
        }
        if (!SatisfiesRange(ver, range)) {
            log::Warn("node", ver + " no satisface '" + range + "'; se ignora");
            return std::nullopt;
        }
        return Runtime{bin, ver, source};
    };

    if (auto b = FindExplicit())
        if (auto r = accept(*b, "env")) return Result<Runtime>::Ok(*r);
    if (auto b = FindSystem())
        if (auto r = accept(*b, "system")) return Result<Runtime>::Ok(*r);
    if (auto b = FindCached())
        if (auto r = accept(*b, "cache")) return Result<Runtime>::Ok(*r);

    log::Info("node", "sin node local utilizable; se descarga el runtime gestionado");
    auto dl = Ensure(range);
    if (!dl.IsOk()) return Result<Runtime>::Err(dl.Error());
    if (auto r = accept(dl.Value(), "downloaded")) return Result<Runtime>::Ok(*r);
    return Result<Runtime>::Err("el runtime descargado no es ejecutable: " + dl.Value().string());
}


} // namespace ow
