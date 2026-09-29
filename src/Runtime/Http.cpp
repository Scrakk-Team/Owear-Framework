// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/Http.cpp — cliente HTTP/1.1 + TLS (OpenSSL) sobre sockets POSIX.
// Sin dependencia de libcurl: GET/POST/PUT/DELETE con headers, body,
// timeout, redirects, Content-Length y chunked.
//
#include "Http.hpp"
#include "Http/Internal.hpp"

#include "../Core/Log.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using ow_socklen_t = int;
  #define OW_SOCK_INVALID INVALID_SOCKET
#else
  #include <netdb.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using ow_socklen_t = socklen_t;
  #define OW_SOCK_INVALID (-1)
#endif

#if defined(__APPLE__)
#include <fcntl.h> // F_SETFD/FD_CLOEXEC para SetCloexec — fuera de todo namespace
#endif

#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <mutex>

namespace ow::http {
Request::Request(std::string method_, std::string url_)
    : method(std::move(method_)), url(std::move(url_)) {}
Request& Request::header(std::string k, std::string v) {
    headers[std::move(k)] = std::move(v);
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

using namespace detail;

bool RequestOnce(const Url& url, const Request& req, Response& resp,
                 std::string& error) {
    std::string payload = req.bodyData;

    // headers por defecto
    std::map<std::string, std::string> hdrs = req.headers;
    if (!hdrs.count("user-agent")) hdrs["User-Agent"] = "owear/0.1 (+https://owear.dev)";
    if (!payload.empty() && !hdrs.count("content-type"))
        hdrs["Content-Type"] = "application/octet-stream";
    if (!payload.empty() && !hdrs.count("content-length"))
        hdrs["Content-Length"] = std::to_string(payload.size());
    hdrs["Connection"] = "close";
    hdrs["Accept"] = "*/*";

    std::string head = req.method + " " + url.path + " HTTP/1.1\r\nHost: " +
                       url.host + "\r\n";
    for (auto& [k, v] : hdrs) head += k + ": " + v + "\r\n";
    head += "\r\n";

    auto converse = [&](Connection& conn) -> bool {
        if (!conn.SendAll(head.data(), head.size())) {
            error = "envío falló";
            return false;
        }
        if (!payload.empty() && !conn.SendAll(payload.data(), payload.size())) {
            error = "envío de body falló";
            return false;
        }
        std::string all;
        if (!conn.ReadAll(all)) {
            error = "lectura falló";
            return false;
        }
        auto sep = all.find("\r\n\r\n");
        if (sep == std::string::npos) {
            error = "respuesta malformada";
            return false;
        }
        if (!ParseHeaders(all.substr(0, sep), resp)) {
            error = "cabeceras inválidas";
            return false;
        }
        std::string rawBody = all.substr(sep + 4);

        auto itChunked = resp.headers.find("transfer-encoding");
        if (itChunked != resp.headers.end() &&
            itChunked->second.find("chunked") != std::string::npos) {
            size_t pos = 0;
            while (pos < rawBody.size()) {
                auto eol = rawBody.find("\r\n", pos);
                if (eol == std::string::npos) break;
                uint64_t sz =
                    std::strtoull(rawBody.substr(pos, eol - pos).c_str(), nullptr, 16);
                if (sz == 0) break;
                size_t dataStart = eol + 2;
                if (dataStart + sz > rawBody.size()) {
                    error = "chunked truncado";
                    return false;
                }
                resp.body.append(rawBody, dataStart, static_cast<size_t>(sz));
                pos = dataStart + sz + 2;
            }
        } else {
            resp.body = std::move(rawBody);
        }
        return true;
    };
    std::unique_ptr<Connection> conn;
    if (!ConnectFor(url, req.timeoutSecs, conn, error)) return false;
    return converse(*conn);
}

} // namespace

Response Perform(const Request& req, std::string& error) {
    constexpr int kMaxRedirects = 8;
    std::string currentUrl = req.url;
    Request r = req;
    for (int i = 0; i <= kMaxRedirects; ++i) {
        Url u;
        if (!ParseUrl(currentUrl, u, error)) {
            error = "URL inválida";
            return {};
        }
        Response resp;
        if (!RequestOnce(u, r, resp, error)) return {};

        if (resp.status == 301 || resp.status == 302 || resp.status == 307 ||
            resp.status == 308) {
            auto loc = resp.headers.find("location");
            if (loc == resp.headers.end()) {
                error = "redirect sin location";
                return {};
            }
            currentUrl = loc->second;
            continue;
        }
        return resp;
    }
    error = "demasiados redirects";
    return {};
}

bool DownloadToString(const std::string& url, std::string& out, std::string& error) {
    auto resp = Perform(Request("GET", url), error);
    if (error.empty()) {
        out.reserve(resp.body.size());
        out = std::move(resp.body);
        return true;
    }
    return false;
}

bool DownloadToFile(const std::string& url, const std::filesystem::path& dest,
                    std::string& error) {
    auto resp = Perform(Request("GET", url), error);
    if (!error.empty()) return false;
    std::error_code ec;
    if (dest.has_parent_path())
        std::filesystem::create_directories(dest.parent_path(), ec);
    std::ofstream f(dest, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "no se pudo crear " + dest.string();
        return false;
    }
    f.write(resp.body.data(), static_cast<std::streamsize>(resp.body.size()));
    if (!f) {
        error = "escritura fallida";
        std::filesystem::remove(dest, ec);
        return false;
    }
    return true;
}

} // namespace ow::http
