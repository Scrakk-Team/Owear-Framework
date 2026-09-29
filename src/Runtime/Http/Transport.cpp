// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/Http/Transport.cpp — fábrica de conexiones (TLS o texto plano).
#include "Transport/Internal.hpp"

#include <memory>
#include <string>

namespace ow::http {
namespace detail {

bool ConnectFor(const Url& url, int timeoutSecs,
                std::unique_ptr<Connection>& out, std::string& error) {
    out = url.tls ? ConnectTls(url, timeoutSecs, error)
                  : ConnectPlain(url, timeoutSecs, error);
    return out != nullptr;
}

} // namespace detail
} // namespace ow::http
