// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/Http/Internal.hpp — helpers internos de ow::http (transporte).
#pragma once
#include "../Http.hpp"

#include <memory>
#include <string>

namespace ow::http {
namespace detail {

/// URL http(s) ya descompuesta.
struct Url {
    std::string host;
    std::string port = "443";
    std::string path = "/";
    bool tls = true;
};

bool ParseUrl(const std::string& url, Url& out, std::string& error);
bool ParseHeaders(const std::string& head, Response& r);

/// Conexión abstracta (TLS o texto plano) usada por RequestOnce.
struct Connection {
    virtual ~Connection() = default;
    virtual bool SendAll(const void* data, size_t len) const = 0;
    virtual bool ReadAll(std::string& out) const = 0;
};

bool ConnectFor(const Url& url, int timeoutSecs, std::unique_ptr<Connection>& out,
                std::string& error);

} // namespace detail
} // namespace ow::http
