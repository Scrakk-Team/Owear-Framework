// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager.hpp — gestor del runtime Node.js.
//
// SIN Node embebido: descarga la release oficial (nodejs.org/dist), verifica
// SHA256 contra SHASUMS256.txt y la cachea por versión. Spawn como sidecar.
//
#pragma once

#include "ow/Common.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ow {

class NodeManager {
public:
    /// Runtime Node localizado, con su procedencia.
    struct Runtime {
        std::filesystem::path bin;  ///< ruta al ejecutable node
        std::string version;        ///< "v22.12.0"
        std::string source;         ///< "env" | "system" | "cache" | "downloaded"
    };

    /// Localiza un runtime Node EN CASCADA, sin descargar si ya hay uno local:
    ///   OW_NODE_BIN → Node del SISTEMA (PATH / ubicaciones del SO) → caché
    ///   → descarga (Ensure).
    /// El sistema se prefiere al caché a propósito: usar el Node que el usuario
    /// ya tiene evita descargas, y además es la única vía que funciona en
    /// Windows, donde la extracción .zip del runtime gestionado no existe.
    static Result<Runtime> Resolve(const std::string& range);

    /// Descarga/verifica/extrae la release oficial (nodejs.org) y la cachea.
    /// Sólo baja: para el caso normal usa Resolve().
    static Result<std::filesystem::path> Ensure(const std::string& range);

    /// Versión mínima aceptada ("v20.0.0"), alineada con `engines` del SDK.
    static std::string MinVersion();

    /// Ejecuta `<bin> --version`; devuelve "v22.12.0" o vacío si no es un node usable.
    static std::string QueryVersion(const std::filesystem::path& bin);

    /// OW_NODE_BIN: ruta explícita del usuario (prioridad absoluta).
    static std::optional<std::filesystem::path> FindExplicit();

    /// Node del sistema: PATH + ubicaciones conocidas por SO.
    static std::optional<std::filesystem::path> FindSystem();

    /// Lanza `node <entryJs>` como proceso hijo (sidecar).
    /// Env: OW_CONTROL_SOCKET, PATH con node al frente. Devuelve pid o -1.
    static long Spawn(const std::filesystem::path& nodeBin, const std::string& entryJs);

    /// Termina el sidecar Node (idempotente). Se llama al salir del kernel.
    static void ShutdownSidecar();

    /// Directorio cache (XDG).
    static std::filesystem::path CacheRoot();

    /// Runtime ya disponible (cache o sistema) sin descargar.
    static std::optional<std::filesystem::path> FindCached();

private:
    struct Release {
        std::string version;   // "v22.4.0"
        bool lts = false;
    };
    static bool FetchIndex(std::vector<Release>& out, std::string& error);
    static const Release* Pick(const std::vector<Release>& list,
                               const std::string& range);
    static std::string PlatformTag();
    static std::string ArchTag();
};

} // namespace ow
