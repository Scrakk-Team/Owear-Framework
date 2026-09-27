// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Session/PermissionBroker.hpp — broker de permisos del WebView.
//
// Los backends (WebKitGTK/WebView2) llaman a `Request` cuando la página pide un
// permiso. Si la app registró un handler, se emite `session.permissionRequest`
// y se espera `session.respondPermission`; si no, se deniega (como Electron).
//
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace ow {

class PermissionBroker {
public:
    static PermissionBroker& Get();

    void SetEnabled(bool on);
    bool Enabled() const { return enabled_; }

    using ResolveCb = std::function<void(bool allow)>;

    /// Resuelve un permiso. `cb` puede llamarse de forma síncrona (denegado) o
    /// asíncrona (cuando responda el main).
    void Request(const std::string& permission, const std::string& origin, ResolveCb cb);

    /// Respuesta de la app para una request pendiente.
    void Resolve(uint64_t id, bool allow);

private:
    bool enabled_ = false;
    std::map<uint64_t, ResolveCb> pending_;
    uint64_t nextId_ = 1;
};

} // namespace ow
