// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/Http/Transport/Internal.hpp — helpers de la capa de transporte.
#pragma once
#include "../Internal.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

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

#include <fcntl.h> // F_SETFD/FD_CLOEXEC para SetCloexec

namespace ow::http {
namespace detail {

void CloseSocket(int fd);
int SendRaw(int fd, const char* p, size_t len);
int RecvRaw(int fd, char* buf, size_t len);
extern const int kSockCloexec;
void SetCloexec(int fd);

/// Fábricas de conexión (implementadas en Tls.cpp / Plain.cpp).
std::unique_ptr<Connection> ConnectTls(const Url& url, int timeoutSecs, std::string& error);
std::unique_ptr<Connection> ConnectPlain(const Url& url, int timeoutSecs, std::string& error);

} // namespace detail
} // namespace ow::http
