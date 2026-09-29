// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/ModuleLoader/Internal.hpp — helpers internos del cargador de módulos.
#pragma once
#include "ow/Module.h"

namespace ow {

/// Host ABI (emit_event/log) compartido con los módulos nativos.
const ow_module_host_t& ModuleHost();

} // namespace ow
