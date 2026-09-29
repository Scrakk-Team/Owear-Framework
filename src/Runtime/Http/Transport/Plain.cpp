// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/Http/Transport/Plain.cpp — conexión de texto plano (TCP).
#include "Internal.hpp"

#include <string>

namespace ow::http {
namespace detail {

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

std::unique_ptr<Connection> ConnectPlain(const Url& url, int timeoutSecs,
                                          std::string& error) {
    auto c = std::make_unique<PlainConnection>();
    if (!c->Connect(url, timeoutSecs, error)) return nullptr;
    return c;
}

} // namespace detail
} // namespace ow::http
