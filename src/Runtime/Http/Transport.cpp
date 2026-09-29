// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/Http/Transport.cpp — capa de transporte (sockets, TLS/OpenSSL, URL).
#include "Internal.hpp"

#include "../../Core/Log.hpp"

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
namespace detail {

#if defined(_WIN32)
inline void CloseSocket(int fd) { ::closesocket(static_cast<SOCKET>(fd)); }
inline int SendRaw(int fd, const char* p, size_t len) {
    return ::send(static_cast<SOCKET>(fd), p, static_cast<int>(len), 0);
}
inline int RecvRaw(int fd, char* buf, size_t len) {
    return ::recv(static_cast<SOCKET>(fd), buf, static_cast<int>(len), 0);
}
struct WsaInit {
    WsaInit() { WSADATA d; ::WSAStartup(MAKEWORD(2, 2), &d); }
    ~WsaInit() { ::WSACleanup(); }
};
const WsaInit g_wsaInit;
#else
inline void CloseSocket(int fd) { ::close(fd); }
inline int SendRaw(int fd, const char* p, size_t len) { return static_cast<int>(::write(fd, p, len)); }
inline int RecvRaw(int fd, char* buf, size_t len) { return static_cast<int>(::read(fd, buf, len)); }
#endif

std::once_flag g_sslInit;

#if defined(__APPLE__)
static constexpr int kSockCloexec = 0; // Darwin: no hay SOCK_CLOEXEC
static void SetCloexec(int fd) { ::fcntl(fd, F_SETFD, FD_CLOEXEC); }
#elif defined(_WIN32)
static constexpr int kSockCloexec = 0; // WinSock2: tampoco define SOCK_CLOEXEC
static void SetCloexec(int) {}
#else
static constexpr int kSockCloexec = SOCK_CLOEXEC;
static void SetCloexec(int) {}
#endif


bool ParseUrl(const std::string& url, Url& out, std::string& error) {
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        error = "URL sin scheme";
        return false;
    }
    out.tls = url.compare(0, schemeEnd, "https") == 0;
    if (!out.tls && url.compare(0, schemeEnd, "http") != 0) {
        error = "solo se soporta http(s)";
        return false;
    }
    if (!out.tls) out.port = "80";
    std::string rest = url.substr(schemeEnd + 3);
    auto pathStart = rest.find('/');
    std::string authority =
        pathStart == std::string::npos ? rest : rest.substr(0, pathStart);
    out.path = pathStart == std::string::npos ? "/" : rest.substr(pathStart);
    auto colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']') == std::string::npos) {
        out.host = authority.substr(0, colon);
        out.port = authority.substr(colon + 1);
    } else {
        out.host = authority;
    }
    if (out.host.empty()) { error = "URL sin host"; return false; }
    return true;
}

// ── transporte ──────────────────────────────────────────────────────────────

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

class PlainConnection : public Connection {
public:
    bool Connect(const Url& url, int timeoutSecs, std::string& error) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (::getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &res) != 0 ||
            !res) {
            error = "DNS falló";
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
        freeaddrinfo(res);
        if (fd_ < 0) { error = "connect falló"; return false; }
        SetCloexec(fd_);
        return true;
    }
    bool SendAll(const void* d, size_t l) const override {
        const char* p = static_cast<const char*>(d);
        while (l > 0) {
            int n = SendRaw(fd_, p, l);
            if (n <= 0) return false;
            p += n;
            l -= static_cast<size_t>(n);
        }
        return true;
    }
    bool ReadAll(std::string& out) const override {
        char buf[16384];
        int n;
        while ((n = RecvRaw(fd_, buf, sizeof(buf))) > 0)
            out.append(buf, static_cast<size_t>(n));
        return true;
    }
    ~PlainConnection() { if (fd_ >= 0) CloseSocket(fd_); }

private:
    int fd_ = -1;
};

bool ParseHeaders(const std::string& head, Response& r) {
    size_t lineEnd = head.find("\r\n");
    if (lineEnd == std::string::npos || head.size() < 12) return false;
    std::istringstream ss(head.substr(9, 3));
    ss >> r.status;
    size_t pos = lineEnd + 2;
    while (pos < head.size()) {
        size_t eol = head.find("\r\n", pos);
        if (eol == std::string::npos) break;
        std::string line = head.substr(pos, eol - pos);
        pos = eol + 2;
        if (line.empty()) break;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        for (auto& c : key) c = static_cast<char>(::tolower(c));
        size_t vstart = colon + 1;
        while (vstart < line.size() && line[vstart] == ' ') ++vstart;
        r.headers[key] = line.substr(vstart);
    }
    return r.status > 0;
}

bool ConnectFor(const Url& url, int timeoutSecs,
                std::unique_ptr<Connection>& out, std::string& error) {
    if (url.tls) {
        auto c = std::make_unique<TlsConnection>();
        if (!c->Connect(url, timeoutSecs, error)) return false;
        out = std::move(c);
        return true;
    }
    auto c = std::make_unique<PlainConnection>();
    if (!c->Connect(url, timeoutSecs, error)) return false;
    out = std::move(c);
    return true;
}

} // namespace detail
} // namespace ow::http
