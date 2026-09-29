// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/ModuleLoader/Paths.cpp — rutas de busqueda de modulos.
#include "../ModuleLoader.hpp"

#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <unistd.h>
#endif
#if defined(__APPLE__)
  #include <mach-o/dyld.h>
#endif

namespace ow {
namespace {

std::vector<std::filesystem::path> SplitPathList(const char* raw) {
    std::vector<std::filesystem::path> out;
    if (!raw || !*raw) return out;
    std::string_view s(raw);
#if defined(_WIN32)
    const char sep = ';'; // ':' colisiona con las unidades (C:\…)
#else
    const char sep = ':';
#endif
    size_t start = 0;
    while (start <= s.size()) {
        auto end = s.find(sep, start);
        if (end == std::string_view::npos) end = s.size();
        if (end > start) out.emplace_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::filesystem::path CurrentExePath() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return {};
    return std::filesystem::path(buf, buf + n);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    std::error_code ec;
    auto p = std::filesystem::canonical(buf, ec);
    return ec ? std::filesystem::path(buf) : p;
#else
    char exeBuf[4096] = {};
    ssize_t n = readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
    if (n <= 0) return {};
    exeBuf[n] = 0;
    return std::filesystem::path(exeBuf);
#endif
}

} // namespace

std::vector<std::filesystem::path> ModuleLoader::SearchPaths() {
    // El build del kernel (<exe>/modules) va PRIMERO: sus módulos son del mismo
    // build. OW_MODULES_DIR (p. ej. el runtime package de la app) va después;
    // si trae un módulo con el mismo nombre, LoadAll lo salta (el del kernel gana).
    std::vector<std::filesystem::path> paths;
    auto exe = CurrentExePath();
    if (!exe.empty()) paths.emplace_back(exe.parent_path() / "modules");
    for (auto& p : SplitPathList(std::getenv("OW_MODULES_DIR"))) paths.push_back(p);

    // Dedup por ruta canónica: OW_MODULES_DIR suele apuntar a <exe>/modules.
    std::vector<std::filesystem::path> unique;
    std::error_code ec;
    for (auto& p : paths) {
        auto c = std::filesystem::weakly_canonical(p, ec);
        const auto& key = ec ? p : c;
        bool dup = false;
        for (const auto& u : unique) {
            std::error_code e2;
            auto uc = std::filesystem::weakly_canonical(u, e2);
            if ((e2 ? u : uc) == key) { dup = true; break; }
        }
        if (!dup) unique.push_back(p);
    }
    return unique;
}


} // namespace ow
