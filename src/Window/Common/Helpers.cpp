// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Common/Helpers.cpp — helpers comunes de Window.
#include "Internal.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>

#include "ow/Shm.h"
#include "ow/detail/minjson.hpp"

namespace ow {
namespace window_detail {

/// Envuelve JS arbitrario para recibir el resultado como JSON string.
std::string WrapEval(const std::string& js) {
    return "(function(){ try { return JSON.stringify((" + js + ")); } catch(e) { "
           "return JSON.stringify({owError: String(e)}); } })()";
}

int CloseVetoTimeoutMs() {
    if (const char* v = std::getenv("OW_CLOSE_TIMEOUT_MS")) {
        int n = atoi(v);
        if (n > 0) return n;
    }
    return 1000; // default: 1 s para responder desde JS
}

/// Bordes aceptados por `beginResizeDrag`. Es la fuente única de la
/// validación: las implementaciones de plataforma traducen estos nombres a
/// sus enums, pero el rechazo de un borde desconocido ocurre aquí.
bool IsValidResizeEdge(const std::string& e) {
    static const char* kEdges[] = {"left",       "right",      "top",
                                   "bottom",     "top-left",   "top-right",
                                   "bottom-left", "bottom-right"};
    for (const char* k : kEdges)
        if (e == k) return true;
    return false;
}

/// Script de resolución de un `invoke`. Si el payload es grande, va por
/// memoria compartida (`_applyShm` + ow-shm://) en vez de embeberse como
/// código JS: evita que el motor compile MB de JavaScript por respuesta.
std::string BuildApplyScript(uint64_t id, bool ok, const std::string& resultJson) {
    static constexpr size_t kInline = 64 * 1024;
    if (resultJson.size() >= kInline) {
        const char* shmId = ow_shm_put(
            reinterpret_cast<const uint8_t*>(resultJson.data()), resultJson.size());
        if (shmId && *shmId) {
            return "window.__ow && window.__ow._applyShm(" + std::to_string(id) + ',' +
                   (ok ? "true" : "false") + ",\"" + shmId + "\"," +
                   std::to_string(resultJson.size()) + ")";
        }
    }
    return "window.__ow && window.__ow._apply(" + std::to_string(id) + ',' +
           (ok ? "true" : "false") + ',' + json::JsLiteral(resultJson) + ")";
}

} // namespace window_detail
} // namespace ow
