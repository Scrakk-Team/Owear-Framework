// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Bridge/Shm/Internal.hpp — helpers internos del registro de SHM.
#pragma once
#include <string>

namespace ow::shm {
namespace shm_detail {

/// Directorio base para los ficheros de región (XDG_RUNTIME_DIR / TMPDIR).
std::string RuntimeDir();

/// Identificador aleatorio de 16 hex chars para una región.
std::string GenId();

/// Limpia regiones huérfanas de procesos anteriores (idempotente).
void SweepStaleRegions();

} // namespace shm_detail
} // namespace ow::shm
