// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/api/installer/src/Internal.hpp — tipos compartidos del builtin installer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace installermod {

/// Entrada del plan de instalación (ruta en el payload → destino relativo).
struct PlanEntry {
    std::string rel;   ///< ruta relativa dentro del payload
    std::string dst;   ///< ruta destino (relativa al directorio de instalación)
    uint64_t size = 0; ///< tamaño en bytes (0 para directorios)
    bool dir = false;  ///< ¿es un directorio?
};

/// Integración con el S.O. (accesos directos, desinstalación, lanzar, elevar).
namespace platform {

bool CreateShortcuts(const std::string& appId, const std::string& appName,
                     const std::string& execPath, const std::string& iconPath,
                     bool desktop, bool menu, bool startup);

bool RemoveShortcuts(const std::string& appId, const std::string& appName);

bool RegisterUninstall(const std::string& appId, const std::string& appName,
                       const std::string& version, const std::string& publisher,
                       const std::string& installDir,
                       const std::string& uninstaller);

bool UnregisterUninstall(const std::string& appId);

bool LaunchDetached(const std::string& path, const std::vector<std::string>& args);

bool IsElevated();

} // namespace platform

} // namespace installermod
