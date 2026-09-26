// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/Http_nossl.cpp — stub de Http sin OpenSSL (OW_WITH_OPENSSL=OFF).
//
// Se usa en builds de DESARROLLO (p.ej. el cross-compile Linux→Windows) donde no
// queremos compilar OpenSSL. La API es idéntica a Http.cpp pero cualquier
// request falla con un error claro. El sidecar Node usa su propio HTTP, así que
// esto no afecta a la app salvo a la descarga del runtime Node (que no ocurre si
// el sistema ya tiene Node).
//
#include "Http.hpp"

namespace ow::http {

Request::Request(std::string method, std::string url)
    : method(std::move(method)), url(std::move(url)) {}

Request& Request::header(std::string k, std::string v) {
    headers.emplace(std::move(k), std::move(v));
    return *this;
}

Request& Request::body(std::string b) {
    bodyData = std::move(b);
    return *this;
}

Request& Request::timeout(int secs) {
    timeoutSecs = secs;
    return *this;
}

namespace {
constexpr const char* kDisabled =
    "HTTP/TLS deshabilitado en este build (OW_WITH_OPENSSL=OFF)";
} // namespace

Response Perform(const Request&, std::string& error) {
    error = kDisabled;
    return {};
}

bool DownloadToFile(const std::string&, const std::filesystem::path&,
                    std::string& error) {
    error = kDisabled;
    return false;
}

bool DownloadToString(const std::string&, std::string&, std::string& error) {
    error = kDisabled;
    return false;
}

} // namespace ow::http
