// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Protocol/ProtocolRegistry.hpp — esquemas personalizados (protocol API).
//
// Un esquema puede servirse de dos formas:
//   · serveDir: el kernel sirve archivos bajo un directorio (rápido, sin IPC).
//   · handler: el kernel reenvía la request al proceso principal (Node) por el
//     control socket y espera su respuesta (`protocol.respond`). Permite servir
//     contenido dinámico (extensiones, assets virtuales…).
//
#pragma once

#include "ow/Common.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ow {

struct ProtocolScheme {
    std::string name;
    bool secure = true;
    bool cors = true;
    bool stream = false;
    bool standard = true;
    bool fetch = true;
    bool serveDir = false;
    std::filesystem::path root;
    bool hasHandler = false;
};

class ProtocolRegistry {
public:
    static ProtocolRegistry& Get();

    void Register(const ProtocolScheme& scheme);
    bool Has(const std::string& name) const;
    const ProtocolScheme* Find(const std::string& name) const;
    std::vector<ProtocolScheme> All() const;

    struct Response {
        int status = 200;
        std::string headersJson = "{}";
        std::string body;      // bytes crudos
        bool resolved = true;  // false → error de resolución
        std::string error;
    };
    using ResolveCb = std::function<void(Response)>;

    /// Resuelve una request de un esquema registrado. Puede ser asíncrona cuando
    /// hay handler en el main (el callback se llama al llegar `protocol.respond`).
    void Dispatch(const std::string& scheme, const std::string& url,
                  const std::string& method, const std::string& headersJson,
                  const std::string& bodyBase64, ResolveCb cb);

    /// Respuesta del proceso principal para una request pendiente.
    void ResolveFromMain(uint64_t reqId, int status, const std::string& headersJson,
                         const std::string& bodyBase64);

    /// Extrae host+path de `scheme://host/path`.
    static std::string UrlPath(const std::string& url);
    static std::string UrlHost(const std::string& url);

private:
    std::map<std::string, ProtocolScheme> schemes_;
    std::map<uint64_t, ResolveCb> pending_;
    uint64_t nextReqId_ = 1;
};

} // namespace ow
