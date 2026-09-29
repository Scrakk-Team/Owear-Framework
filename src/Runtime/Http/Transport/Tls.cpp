// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/Http/Transport/Tls.cpp — conexión TLS (OpenSSL).
#include "Internal.hpp"
#include "../../../Core/Log.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <mutex>
#include <string>

namespace ow::http {
namespace detail {

namespace {
std::once_flag g_sslInit;
} // namespace

class TlsConnection : public Connection {
public:
    ~TlsConnection() {
        if (ssl_) SSL_free(ssl_);
        if (fd_ >= 0) CloseSocket(fd_);
        // ctx_ es singleton del proceso (compartido) — NUNCA se libera aquí
    }

    bool Connect(const Url& url, int timeoutSecs, std::string& error) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        int rc = ::getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &res);
        if (rc != 0 || !res) {
            error = "DNS falló para " + url.host;
            return false;
        }
        for (addrinfo* p = res; p; p = p->ai_next) {
#if defined(_WIN32)
            fd_ = static_cast<int>(::socket(p->ai_family, p->ai_socktype, p->ai_protocol));
#else
            fd_ = ::socket(p->ai_family, p->ai_socktype | kSockCloexec, p->ai_protocol);
#endif
            if (fd_ < 0) continue;
#if defined(_WIN32)
            DWORD tv = static_cast<DWORD>(timeoutSecs) * 1000;
            setsockopt(static_cast<SOCKET>(fd_), SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&tv), sizeof(tv));
            setsockopt(static_cast<SOCKET>(fd_), SOL_SOCKET, SO_SNDTIMEO,
                      reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
            timeval tv{timeoutSecs, 0};
            setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
            if (::connect(fd_, p->ai_addr, static_cast<ow_socklen_t>(p->ai_addrlen)) == 0) break;
            CloseSocket(fd_);
            fd_ = -1;
        }
        ::freeaddrinfo(res);
        if (fd_ < 0) { error = "connect falló"; return false; }

        static SSL_CTX* sharedCtx = nullptr;
        std::call_once(g_sslInit, [] {
            OPENSSL_init_ssl(
                OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
                nullptr);
            sharedCtx = SSL_CTX_new(TLS_client_method());
            if (sharedCtx) SSL_CTX_set_default_verify_paths(sharedCtx);
        });
        ctx_ = sharedCtx;
        if (!ctx_) { error = "SSL_CTX_new falló"; return false; }

        ssl_ = SSL_new(ctx_);
        SSL_set_fd(ssl_, fd_);
        SSL_set_tlsext_host_name(ssl_, url.host.c_str());
        X509_VERIFY_PARAM* param = SSL_get0_param(ssl_);
        X509_VERIFY_PARAM_set1_host(param, url.host.c_str(), 0);
        SSL_set_verify(ssl_, SSL_VERIFY_PEER, nullptr);

        if (SSL_connect(ssl_) != 1) {
            unsigned long e = ERR_get_error();
            char buf[256];
            ERR_error_string_n(e, buf, sizeof(buf));
            error = std::string("TLS: ") + buf;
            return false;
        }
        return true;
    }

    bool SendAll(const void* data, size_t len) const override {
        const char* p = static_cast<const char*>(data);
        while (len > 0) {
            int n = SSL_write(ssl_, p, static_cast<int>(len));
            if (n <= 0) return false;
            p += n;
            len -= static_cast<size_t>(n);
        }
        return true;
    }

    bool ReadAll(std::string& out) const override {
        char buf[16384];
        while (true) {
            int n = SSL_read(ssl_, buf, sizeof(buf));
            if (n > 0) {
                out.append(buf, static_cast<size_t>(n));
                continue;
            }
            int err = SSL_get_error(ssl_, n);
            if (err == SSL_ERROR_ZERO_RETURN) return true;
#if defined(_WIN32)
            if (err == SSL_ERROR_SYSCALL) return true;
#else
            if (err == SSL_ERROR_SYSCALL && errno == ECONNRESET) return true;
#endif
            return false;
        }
    }

private:
    int fd_ = -1;
    SSL_CTX* ctx_ = nullptr;
    SSL* ssl_ = nullptr;
};


std::unique_ptr<Connection> ConnectTls(const Url& url, int timeoutSecs,
                                        std::string& error) {
    auto c = std::make_unique<TlsConnection>();
    if (!c->Connect(url, timeoutSecs, error)) return nullptr;
    return c;
}

} // namespace detail
} // namespace ow::http
