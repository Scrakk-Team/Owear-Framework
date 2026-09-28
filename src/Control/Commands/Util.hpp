// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/Util.hpp — utilidades compartidas de los comandos.
#pragma once

#include "../../Bridge/Dispatcher.hpp"
#include "ow/Common.h"
#include "ow/detail/minjson.hpp"

#include <cstdint>

namespace ow {

/// Ventana con foco (window.getFocused). La actualiza WireWindowEvents.
extern WindowId g_focusedWindow;

/// PID del proceso del kernel.
uint32_t CtCurrentPid();

/// Metadatos de un módulo → JSON (para module.list/module.info).
json::Object CtModuleInfoJson(const Dispatcher::ModuleInfo& m);

} // namespace ow
