// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/Http/Transport/Shims.cpp — close/send/recv y cloexec por plataforma.
#include "Internal.hpp"

namespace ow::http {
namespace detail {

#if defined(_WIN32)
void CloseSocket(int fd) { ::closesocket(static_cast<SOCKET>(fd)); }
int SendRaw(int fd, const char* p, size_t len) {
    return ::send(static_cast<SOCKET>(fd), p, static_cast<int>(len), 0);
}
int RecvRaw(int fd, char* buf, size_t len) {
    return ::recv(static_cast<SOCKET>(fd), buf, static_cast<int>(len), 0);
}
struct WsaInit {
    WsaInit() { WSADATA d; ::WSAStartup(MAKEWORD(2, 2), &d); }
    ~WsaInit() { ::WSACleanup(); }
};
const WsaInit g_wsaInit;
#else
void CloseSocket(int fd) { ::close(fd); }
int SendRaw(int fd, const char* p, size_t len) { return static_cast<int>(::write(fd, p, len)); }
int RecvRaw(int fd, char* buf, size_t len) { return static_cast<int>(::read(fd, buf, len)); }
#endif
#if defined(__APPLE__)
extern const int kSockCloexec = 0; // Darwin: no hay SOCK_CLOEXEC
void SetCloexec(int fd) { ::fcntl(fd, F_SETFD, FD_CLOEXEC); }
#elif defined(_WIN32)
extern const int kSockCloexec = 0; // WinSock2: tampoco define SOCK_CLOEXEC
void SetCloexec(int) {}
#else
extern const int kSockCloexec = SOCK_CLOEXEC;
void SetCloexec(int) {}
#endif

} // namespace detail
} // namespace ow::http
