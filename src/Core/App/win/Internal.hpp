// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/App/win/Internal.hpp — bomba de mensajes y crash handlers (Win32).
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>

namespace ow::internal {

UINT PumpMessageId();
void PumpSetThreadId(DWORD id);
DWORD PumpThreadId();
void PumpSetHwnd(HWND hwnd);
HWND PumpHwnd();

LRESULT CALLBACK PumpWndProc(HWND, UINT, WPARAM, LPARAM);

/// Encola un callback y arma la bomba (un único mensaje en vuelo).
void PumpPost(std::function<void()> fn);
/// Drena la cola de callbacks pendientes.
void PumpDrain();

/// Instala el filtro global + el de primera oportunidad.
void InstallCrashHandlers();

} // namespace ow::internal
