// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Common/Internal.hpp — helpers internos de Window (común).
#pragma once
#include <cstdint>
#include <string>

namespace ow {
namespace window_detail {

/// Envuelve JS arbitrario para recibir el resultado como JSON string.
std::string WrapEval(const std::string& js);

/// Timeout del flujo de cierre con veto JS (OW_CLOSE_TIMEOUT_MS, def. 1000 ms).
int CloseVetoTimeoutMs();

/// Bordes aceptados por `beginResizeDrag` (fuente única de la validación).
bool IsValidResizeEdge(const std::string& e);

/// Script de resolución de un `invoke` (inline o vía SHM `_applyShm`).
std::string BuildApplyScript(uint64_t id, bool ok, const std::string& resultJson);

} // namespace window_detail
} // namespace ow
