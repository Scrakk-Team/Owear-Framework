// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/App/win/Exceptions.cpp — filtros de excepcion (diagnostico, Win32).
#include "Internal.hpp"
#include "../../Log.hpp"

#include <string>

namespace ow::internal {

namespace {

// Diagnóstico: los crashes silenciosos (p.ej. en la creación de WebView2)
// matan el proceso sin una sola línea de log. Esto registra al menos el
// código de excepción y el módulo donde ocurrió.
LONG WINAPI OwUnhandledFilter(EXCEPTION_POINTERS* info) {
    log::Error("app", "excepción no manejada: código 0x" +
                          std::to_string(static_cast<unsigned long>(
                              info->ExceptionRecord->ExceptionCode)));
    return EXCEPTION_EXECUTE_HANDLER;
}

// Primera oportunidad: registra TODAS las excepciones, incluidos los
// fail-fast que no llegan al filtro global ni al __except.
LONG CALLBACK OwVectoredHandler(PEXCEPTION_POINTERS info) {
    const ULONG_PTR code =
        info && info->ExceptionRecord
            ? info->ExceptionRecord->ExceptionCode
            : 0;
    // filtra ruido benigno de C++/guard pages
    if (code != 0xE06D7363 /*C++ throw*/ &&
        code != 0x80000001 /*guard page*/) {
        log::Error("app", "excepción (1a oportunidad): 0x" +
                              std::to_string(static_cast<unsigned long>(code)) +
                              " en addr=0x" + std::to_string(
                                  reinterpret_cast<uintptr_t>(
                                      info->ExceptionRecord
                                          ->ExceptionAddress)));
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void InstallCrashHandlers() {
    SetUnhandledExceptionFilter(&OwUnhandledFilter);
    AddVectoredExceptionHandler(1, &OwVectoredHandler);
}

} // namespace ow::internal
