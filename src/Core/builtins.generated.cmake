# ─────────────────────────────────────────────────────────────────────────────
# GENERADO por tools/gen-apis.mjs — NO EDITAR A MANO.
# Fuente de verdad: api/<nombre>/owear.module.json (kind=builtin)
# Regenerar: node tools/gen-apis.mjs
# ─────────────────────────────────────────────────────────────────────────────


set(OW_BUILTIN_SOURCES_linux
    ${CMAKE_SOURCE_DIR}/src/api/app/src/app_builtin.cpp
    ${CMAKE_SOURCE_DIR}/src/api/crashreporter/src/crashreporter.cpp
    ${CMAKE_SOURCE_DIR}/src/api/installer/src/installer.cpp
    ${CMAKE_SOURCE_DIR}/src/api/installer/src/shortcuts_linux.cpp
    ${CMAKE_SOURCE_DIR}/src/api/session/src/session_builtin.cpp
    ${CMAKE_SOURCE_DIR}/src/api/webview/src/webview.cpp
    ${CMAKE_SOURCE_DIR}/src/api/window/src/window_extra.cpp)

set(OW_BUILTIN_SOURCES_win
    ${CMAKE_SOURCE_DIR}/src/api/installer/src/installer.cpp
    ${CMAKE_SOURCE_DIR}/src/api/installer/src/shortcuts_win.cpp
    ${CMAKE_SOURCE_DIR}/src/api/webview/src/webview.cpp
    ${CMAKE_SOURCE_DIR}/src/api/window/src/window_extra_win.cpp)

set(OW_BUILTIN_SOURCES_mac
    ${CMAKE_SOURCE_DIR}/src/api/window/src/window_extra_mac.mm)

# Selección por plataforma (OW_PLATFORM: linux|win|mac).
set(OW_BUILTIN_SOURCES ${OW_BUILTIN_SOURCES_${OW_PLATFORM}})
