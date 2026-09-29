// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Platform/win/Internal.hpp — helpers internos del transporte (named pipe).
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ow {
namespace control_win_detail {

/// Verificación mínima de "mismo usuario" para el pipe de control (impersona,
/// compara el SID y revierte). Ver comentario original en SameUser.cpp.
bool ClientIsSameUser(HANDLE pipeHandle);

} // namespace control_win_detail
} // namespace ow
