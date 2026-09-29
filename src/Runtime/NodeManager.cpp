// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager.cpp — descarga/verificación/extracción (común).
// Spawn por plataforma en NodeManager_<platform>.cpp.
//
#include "NodeManager.hpp"
#include "NodeManager/Internal.hpp"

#include "../Core/Log.hpp"
#include "Http.hpp"
#include "Sha256.hpp"
#include "TarGz.hpp"
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

using namespace nodemanager_detail;

namespace fs = std::filesystem;

std::filesystem::path NodeManager::CacheRoot() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    if (xdg && *xdg) return fs::path(xdg) / "owear" / "node";
#ifdef _WIN32
    const char* la = std::getenv("LOCALAPPDATA");
    if (la && *la) return fs::path(la) / "owear" / "cache" / "node";
#endif
    const char* home = std::getenv("HOME");
    return fs::path(home && *home ? home : "/tmp") / ".cache" / "owear" / "node";
}

std::string NodeManager::PlatformTag() {
#ifdef _WIN32
    return "win";
#elif defined(__APPLE__)
    return "darwin";
#else
    return "linux";
#endif
}

std::string NodeManager::ArchTag() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__)
    return "armv7l";
#elif defined(__riscv) && __riscv_xlen == 64
    return "riscv64";
#else
    return "x64";
#endif
}

bool NodeManager::FetchIndex(std::vector<Release>& out, std::string& error) {
    std::string body;
    if (!http::DownloadToString("https://nodejs.org/dist/index.json", body, error))
        return false;
    auto parsed = json::Parse(body);
    if (!parsed.value || !parsed.value->IsArray()) {
        error = "index.json inválido";
        return false;
    }
    for (const auto& e : parsed.value->AsArray()) {
        Release r;
        if (const json::Value* v = e.Find("version"); v && v->IsString())
            r.version = v->AsString();
        if (const json::Value* v = e.Find("lts"); v)
            r.lts = !v->IsNull(); // "lts" es codename string cuando aplica, null si no
        if (!r.version.empty()) out.push_back(std::move(r));
    }
    return !out.empty();
}


const NodeManager::Release* NodeManager::Pick(const std::vector<Release>& list,
                                              const std::string& range) {
    const Release* best = nullptr;
    if (range.empty() || range == "latest") {
        for (const auto& r : list)
            if (!best || SemverCompare(r.version, best->version) > 0) best = &r;
        return best;
    }
    if (range == "lts") {
        for (const auto& r : list)
            if (r.lts && (!best || SemverCompare(r.version, best->version) > 0)) best = &r;
        return best;
    }
    // exacto o prefijo de major: "v22" | "22" | "v22.4.0"
    std::string want = range;
    if (!want.empty() && want[0] != 'v') want = "v" + want;
    for (const auto& r : list) {
        bool match = r.version == want;
        if (!match) {
            // prefijo: v22.x.x
            auto dot = want.find('.', 1);
            std::string prefix = dot == std::string::npos ? want : want.substr(0, dot);
            match = r.version.compare(0, prefix.size(), prefix) == 0 &&
                    (r.version.size() == prefix.size() || r.version[prefix.size()] == '.');
        }
        if (match && (!best || SemverCompare(r.version, best->version) > 0)) best = &r;
    }
    return best;
}

std::optional<fs::path> NodeManager::FindCached() {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(CacheRoot(), ec)) {
        if (!e.is_directory()) continue;
#ifdef _WIN32
        auto bin = e.path() / "node.exe";
#else
        auto bin = e.path() / "bin" / "node";
#endif
        if (fs::exists(bin, ec)) return bin;
    }
    return std::nullopt;
}


} // namespace ow
