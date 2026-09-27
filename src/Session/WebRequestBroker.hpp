// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Session/WebRequestBroker.hpp — intercepción de requests (webRequest).
//
// La app registra patrones (`urls`) y un handler. El backend consulta aquí cada
// request; si coincide, se reenvía al main (`webRequest.request`) y su respuesta
// (`webRequest.respond`) decide: cancelar / redirigir / dejar pasar.
//
// Plataformas:
//   · Windows: todos los requests (`WebResourceRequested`, con deferral).
//   · Linux: navegaciones (`decide-policy`). WebKitGTK 2.52 ya no expone
//     `WebKitWebPage::send-request`, así que los subrecursos no se interceptan.
//
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ow {

class WebRequestBroker {
public:
    static WebRequestBroker& Get();

    /// Registra los patrones (globs) y activa la intercepción.
    void Register(const std::vector<std::string>& patterns);
    void Unregister();
    bool Enabled() const { return enabled_; }
    bool Matches(const std::string& url) const;

    struct Action {
        bool cancel = false;
        std::string redirectUrl;
    };
    using Cb = std::function<void(Action)>;

    /// Consulta al main. `cb` se llama de forma síncrona (sin handler / sin
    /// coincidencia → dejar pasar) o asíncrona (cuando responde el main).
    void BeforeRequest(const std::string& url, const std::string& method,
                       const std::string& headersJson, Cb cb);

    void Resolve(uint64_t id, bool cancel, const std::string& redirectUrl);

    /// Matcher glob (`*`, `?`) tipo Electron; soporta `<all_urls>`.
    static bool GlobMatch(const std::string& pattern, const std::string& url);

private:
    bool enabled_ = false;
    std::vector<std::string> patterns_;
    std::map<uint64_t, Cb> pending_;
    uint64_t nextId_ = 1;
};

} // namespace ow
