// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#include "Util.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ow {

using V = json::Value;

WindowId g_focusedWindow = 0;

uint32_t CtCurrentPid() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

json::Object CtModuleInfoJson(const Dispatcher::ModuleInfo& m) {
    json::Object o;
    o.emplace_back("name", V(m.name));
    o.emplace_back("version", V(m.version));
    o.emplace_back("origin", V(m.origin));
    o.emplace_back("builtin", V(m.builtin));
    o.emplace_back("functions", V(static_cast<int64_t>(m.functions.size())));
    json::Array names;
    for (const auto& fn : m.functions) names.emplace_back(V(fn));
    o.emplace_back("functionNames", V(std::move(names)));
    return o;
}

} // namespace ow
