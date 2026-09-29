// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#include "ModuleLoader.hpp"
#include "ModuleLoader/Internal.hpp"

#include "Log.hpp"
#include "../Bridge/Dispatcher.hpp"
#include "ow_api.h"
#include "../Bridge/Shm.hpp"
#include "../Control/ControlServer.hpp"
#include "ow/App.h"
#include "ow/Bridge/Codec.h"
#include "ow/Window.h"
#include "ow/detail/minjson.hpp"

#include <set>

#include <map>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
  #include <unistd.h>
#endif
#if defined(__APPLE__)
  #include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <mutex>

// receptor del host para builtins (definido en BuiltinUtil_<plat>)
#if defined(OW_BUILTINS_GTK)
extern "C" void ow_builtin_receive_host(const ::ow_module_host_t* h);
#endif

namespace ow {

namespace {
std::mutex g_mu;
std::vector<void*> g_handles;
} // namespace
size_t ModuleLoader::RegisterStatic(const ow_module_desc_t* desc) {
    return Dispatcher::Get().RegisterModule(desc, "builtin");
}

size_t ModuleLoader::LoadFile(const std::filesystem::path& file) {
#if defined(_WIN32)
    HMODULE handle = ::LoadLibraryW(file.c_str());
    if (!handle) {
        log::Error("loader", "LoadLibrary falló para " + file.string() +
                                  " (err " + std::to_string(::GetLastError()) + ")");
        return 0;
    }
    auto entry = reinterpret_cast<ow_module_entry_t>(
        reinterpret_cast<void*>(::GetProcAddress(handle, "ow_module_descriptor")));
    if (!entry) {
        log::Error("loader", file.string() + ": símbolo ow_module_descriptor ausente");
        ::FreeLibrary(handle);
        return 0;
    }
    if (auto setHost = reinterpret_cast<ow_module_set_host_t>(reinterpret_cast<void*>(
            ::GetProcAddress(handle, "ow_module_set_host"))); setHost) {
        setHost(&ModuleHost());
    }
    const ow_module_desc_t* desc = entry();
    size_t count = Dispatcher::Get().RegisterModule(desc, file.filename().string());
    if (count > 0) {
        std::lock_guard lock(g_mu);
        g_handles.push_back(reinterpret_cast<void*>(handle));
    } else {
        ::FreeLibrary(handle);
    }
    return count;
#else
    void* handle = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        log::Error("loader", std::string("dlopen falló: ") + dlerror());
        return 0;
    }
    auto entry = reinterpret_cast<ow_module_entry_t>(dlsym(handle, "ow_module_descriptor"));
    if (!entry) {
        log::Error("loader", file.string() + ": símbolo ow_module_descriptor ausente");
        dlclose(handle);
        return 0;
    }
    // host callbacks (emit_event/log) — opcional para el módulo
    if (auto setHost = reinterpret_cast<ow_module_set_host_t>(
            dlsym(handle, "ow_module_set_host")); setHost) {
        setHost(&ModuleHost());
    }
    const ow_module_desc_t* desc = entry();
    size_t count = Dispatcher::Get().RegisterModule(desc, file.filename().string());
    if (count > 0) {
        std::lock_guard lock(g_mu);
        g_handles.push_back(handle);
    } else {
        dlclose(handle);
    }
    return count;
#endif
}

size_t ModuleLoader::LoadAll() {
    size_t total = 0;
    std::set<std::string> seen; // dedup por nombre: el 1er dir (<exe>/modules) gana
    for (const auto& dir : SearchPaths()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
#if defined(_WIN32)
            if (ext != ".dll" && ext != ".owm") continue;
#elif defined(__APPLE__)
            if (ext != ".dylib" && ext != ".so" && ext != ".owm") continue;
#else
            if (ext != ".so" && ext != ".owm") continue;
#endif
            if (!seen.insert(e.path().filename().string()).second) continue;
            total += LoadFile(e.path());
        }
    }
    return total;
}

void ModuleLoader::ProvideHostToBuiltins() {
#if defined(OW_BUILTINS_GTK)
    ::ow_builtin_receive_host(static_cast<const ::ow_module_host_t*>(&ModuleHost()));
#endif
}

void ModuleLoader::Shutdown() {
    std::lock_guard lock(g_mu);
#if defined(_WIN32)
    for (void* h : g_handles) ::FreeLibrary(reinterpret_cast<HMODULE>(h));
#else
    for (void* h : g_handles) dlclose(h);
#endif
    g_handles.clear();
}

} // namespace ow
