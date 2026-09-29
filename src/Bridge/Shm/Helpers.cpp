// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Bridge/Shm/Helpers.cpp — rutas, ids y barrido de regiones SHM.
#include "Internal.hpp"
#include "../Shm.hpp"
#include "../../Core/Log.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>

namespace ow::shm {
namespace shm_detail {

std::string RuntimeDir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && *xdg) return xdg;
#ifdef _WIN32
    const char* la = std::getenv("LOCALAPPDATA");
    if (la && *la) return std::string(la) + "\\Temp";
#endif
    const char* tmp = std::getenv("TMPDIR");
    return (tmp && *tmp) ? tmp : "/tmp";
}
std::string GenId() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(rng()));
    return buf;
}
void SweepStaleRegions() {
    static bool done = false;
    if (done) return;
    done = true;
    std::error_code ec;
    const std::string dir = RuntimeDir();
    for (std::filesystem::directory_iterator it(dir, ec), end; it != end;
         it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (name.rfind("owear-shm-", 0) == 0) {
            std::error_code e2;
            std::filesystem::remove(it->path(), e2);
        }
    }
}

} // namespace shm_detail
} // namespace ow::shm
