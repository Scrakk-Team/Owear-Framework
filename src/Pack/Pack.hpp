// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Pack/Pack.hpp — payload embebido en el propio binario (single binary).
//
// Formato (al final del ejecutable):
//   [ kernel ][ payload.tar.gz ][ footer (24 B) ]
//   footer = "OWPK1\0\0\0" (8) + offset(u64 LE) + size(u64 LE)
//
// El kernel lee SU PROPIA imagen, localiza el índice y extrae el payload a una
// caché (una sola vez), dejando módulos/assets/main listos como si fueran
// ficheros normales (OW_MODULES_DIR/OW_ASSETS_DIR/OW_APP_MAIN).
#pragma once

#include <string>

namespace ow::pack {

/// ¿El binario actual trae payload embebido?
bool HasPayload();

/// Extrae el payload a una carpeta de caché (idempotente) y devuelve su ruta.
/// Cadena vacía si no hay payload o falla.
std::string EnsureExtracted();

/// JSON del manifest (`manifest.json` dentro del payload), o "" si no hay.
std::string Manifest();

} // namespace ow::pack
