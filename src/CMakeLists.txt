# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# ── Owear kernel + plataforma ────────────────────────────────────────────────
# Selección de fuentes POR PLATAFORMA vía CMake (nunca #ifdef de features).
# Cambiar los contratos en include/ow/ rompe las 3 plataformas aquí, en compile-time.

find_package(Threads REQUIRED)

if(OW_PLATFORM STREQUAL "linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.1)
    pkg_check_modules(OPENSSL REQUIRED openssl)
    pkg_check_modules(GIO_UNIX REQUIRED gio-unix-2.0)
elseif(OW_PLATFORM STREQUAL "mac")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPENSSL REQUIRED openssl)
    pkg_check_modules(GLIB2 REQUIRED glib-2.0 gio-2.0)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
endif()

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# OpenSSL (HTTPS + módulos net/updater). Se puede desactivar para builds de dev
# (p.ej. cross-compile Linux→Windows) → se usa Runtime/Http_nossl.cpp.
option(OW_WITH_OPENSSL "Compilar con OpenSSL (HTTPS y módulos net/updater)" ON)

set(OW_HTTP_SOURCE Runtime/Http.cpp Runtime/Http/Transport.cpp)
if(NOT OW_WITH_OPENSSL)
    set(OW_HTTP_SOURCE Runtime/Http_nossl.cpp)
endif()

set(OW_KERNEL_SOURCES
    Core/App.cpp
    Core/Log.cpp
    Core/ModuleLoader.cpp
    Core/ModuleLoader/Host.cpp
    Core/ModuleLoader/Paths.cpp
    Core/WindowModule.cpp
    Core/NodeBridge.cpp
    Core/BuiltinRegistry.generated.cpp   # generado por tools/gen-apis.mjs
    Bridge/Codec.cpp
    Bridge/Codec/Scanner.cpp
    Bridge/BridgeScript.cpp
    Bridge/Dispatcher.cpp
    Bridge/Shm.cpp
    Bridge/Shm/Helpers.cpp
    Control/ControlServer.cpp
    Control/Commands/Util.cpp
    Control/Commands/App.cpp
    Control/Commands/Node.cpp
    Control/Commands/Module.cpp
    Control/Commands/Window.cpp
    Control/Commands/Menu.cpp
    Control/Commands/Protocol.cpp
    Control/Commands/Session.cpp
    Control/Commands/WebContents.cpp
    Control/Commands/WebRequest.cpp
    Control/Commands/NodeBridge.cpp
    Protocol/ProtocolRegistry.cpp
    Session/PermissionBroker.cpp
    Session/WebRequestBroker.cpp
    Session/WindowOpenBroker.cpp
    Window/Window_common.cpp
    Window/Common/Helpers.cpp
    Window/Common/Messages.cpp
    Window/Common/Outbox.cpp
    Runtime/Sha256.cpp
    Runtime/TarGz.cpp
    Runtime/TarGz/Helpers.cpp
    Pack/Pack.cpp
    Pack/Helpers.cpp
    ${OW_HTTP_SOURCE}
    Runtime/NodeManager.cpp
    Runtime/NodeManager/Semver.cpp
    Runtime/NodeManager/Resolve.cpp
)

# Nota: las fuentes de los builtins (app/session/crashreporter/window-extras)
# NO se listan aquí. Se generan desde los manifiestos api/<nombre>/owear.module.json
# (kind=builtin) → src/Core/builtins.generated.cmake, y se anexan más abajo.

if(OW_PLATFORM STREQUAL "linux")
    list(APPEND OW_KERNEL_SOURCES
        Core/App_linux.cpp
        Window/window_linux.cpp
    Window/Platform/linux/Theme.cpp
    Window/Platform/linux/ViewEvents.cpp
    Window/Platform/linux/Titlebar.cpp
    Window/Platform/linux/Titlebar/Overlay.cpp
    Window/Platform/linux/Titlebar/Edges.cpp
    Window/Platform/linux/Create.cpp
    Window/Platform/linux/Webviews/Create.cpp
    Window/Platform/linux/Webviews/Commands.cpp
        Webview/linux/WebKitGTKBackend.cpp
        Webview/linux/Helpers.cpp
        Webview/linux/Backend/Protocols.cpp
        Webview/linux/BuiltinUtil_linux.cpp
        Control/ControlServer_linux.cpp
        Runtime/NodeManager_linux.cpp
    )
elseif(OW_PLATFORM STREQUAL "win")
    if(OW_WITH_OPENSSL)
        find_package(OpenSSL REQUIRED) # Http.cpp (TLS): el toolchain de vcpkg da
                                       # los headers, pero el .lib hay que enlazarlo
    endif()
    # ── WebView2: auto-detección del SDK vendido (patrón deps/ de ole) ──────
    if(NOT DEFINED OW_WITH_WEBVIEW2)
        if(EXISTS "${CMAKE_SOURCE_DIR}/deps/webview2/include/WebView2.h")
            set(OW_WITH_WEBVIEW2 ON)
        else()
            set(OW_WITH_WEBVIEW2 OFF)
        endif()
    elseif(OW_WITH_WEBVIEW2 AND
           NOT EXISTS "${CMAKE_SOURCE_DIR}/deps/webview2/include/WebView2.h")
        message(WARNING "OW_WITH_WEBVIEW2=ON pero falta deps/webview2 — stub")
        set(OW_WITH_WEBVIEW2 OFF)
    endif()
    message(STATUS "Owear backend WebView2: ${OW_WITH_WEBVIEW2}")

    list(APPEND OW_KERNEL_SOURCES
        Core/App_win.cpp
        Window/window_win.cpp
    Window/Platform/win/Titlebar.cpp
    Window/Platform/win/Titlebar/Drawing.cpp
    Window/Platform/win/Menu.cpp
    Window/Platform/win/Util.cpp
    Window/Platform/win/Webviews.cpp
    Window/Platform/win/Webviews/Helpers.cpp
    Window/Platform/win/Events.cpp
    Window/Platform/win/Create.cpp
    Window/Platform/win/Methods.cpp
        Control/ControlServer_win.cpp
        Runtime/NodeManager_win.cpp
        Webview/win/BuiltinUtil_win.cpp
    )
    if(OW_WITH_WEBVIEW2)
        list(APPEND OW_KERNEL_SOURCES Webview/win/Webview2Backend.cpp
                                     Webview/win/Helpers.cpp
                                     Webview/win/Backend/Controller.cpp
                                     Webview/win/Backend/Handlers.cpp)
    else()
        # Stub sin WebView2 SDK: CreateWebviewBackend() → nullptr con log claro.
        list(APPEND OW_KERNEL_SOURCES Webview/win/Webview2Stub_win.cpp)
    endif()
elseif(OW_PLATFORM STREQUAL "mac")
    list(APPEND OW_KERNEL_SOURCES
        Core/App_mac.mm
        Window/window_mac.mm
        Webview/mac/WKWebviewBackend.mm
        Control/ControlServer_mac.mm
        Runtime/NodeManager_mac.mm
        Webview/mac/BuiltinUtil_mac.mm
    )
endif()

# ── Builtins: fuentes generadas desde manifiestos (kind=builtin) ─────────────
# No hardcodear aquí: sale de api/<nombre>/owear.module.json.
# Regenerar: node tools/gen-apis.mjs
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Core/builtins.generated.cmake)
    message(FATAL_ERROR "src/Core/builtins.generated.cmake ausente — corre: node tools/gen-apis.mjs")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/Core/builtins.generated.cmake)
# En Windows sin WebView2 no existe el builtin nativo de window-extras.
if(OW_PLATFORM STREQUAL "win" AND NOT OW_WITH_WEBVIEW2)
    set(OW_BUILTIN_SOURCES)
endif()
list(APPEND OW_KERNEL_SOURCES ${OW_BUILTIN_SOURCES})

add_library(owear_core STATIC ${OW_KERNEL_SOURCES})

if(OW_PLATFORM STREQUAL "linux")
    target_compile_definitions(owear_core PUBLIC OW_BUILTINS_GTK=1)
endif()

target_include_directories(owear_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_definitions(owear_core PUBLIC OW_VERSION_STRING="0.1.0")
if(OW_PLATFORM STREQUAL "win")
    target_compile_definitions(owear_core PUBLIC OW_PLATFORM_WIN=1)
endif()

target_link_libraries(owear_core PUBLIC Threads::Threads ZLIB::ZLIB)

if(OW_PLATFORM STREQUAL "win")
    if(OW_WITH_OPENSSL)
        target_link_libraries(owear_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif()
    if(OW_WITH_WEBVIEW2)
        target_include_directories(owear_core PRIVATE
            ${CMAKE_SOURCE_DIR}/deps/webview2/include)
        target_link_libraries(owear_core PUBLIC
            "${CMAKE_SOURCE_DIR}/deps/webview2/lib/x64/WebView2LoaderStatic.lib")
    endif()
endif()

if(OW_PLATFORM STREQUAL "linux")
    target_include_directories(owear_core PRIVATE
        ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIRS}
        ${GIO_UNIX_INCLUDE_DIRS})
    target_link_libraries(owear_core PUBLIC
        ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} ${OPENSSL_LIBRARIES} ${GIO_UNIX_LIBRARIES})
    target_link_directories(owear_core PUBLIC
        ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
    target_compile_options(owear_core PRIVATE -Wall -Wextra)
endif()

# ── macOS: OpenSSL (Http) + GLib (ControlServer) + Cocoa/WebKit (UI) ────────
if(OW_PLATFORM STREQUAL "mac")
    target_include_directories(owear_core PRIVATE
        ${OPENSSL_INCLUDE_DIRS} ${GLIB2_INCLUDE_DIRS})
    # Homebrew es keg-only: sin estos dirs el enlazador no halla -lssl/-lglib
    target_link_directories(owear_core PUBLIC
        ${OPENSSL_LIBRARY_DIRS} ${GLIB2_LIBRARY_DIRS})
    target_link_libraries(owear_core PUBLIC
        ${OPENSSL_LIBRARIES} ${GLIB2_LIBRARIES}
        ${COCOA_FRAMEWORK} ${WEBKIT_FRAMEWORK})
endif()

# ── Binario del runtime ──────────────────────────────────────────────────────
add_executable(owear main_kernel.cpp)
target_link_libraries(owear PRIVATE owear_core)
# Import library del exe para que los .owm (MSVC) resuelvan ow_shm_* del host.
set_target_properties(owear PROPERTIES ENABLE_EXPORTS ON)
if(MSVC)
    # Los .owm ven __declspec(dllimport) (OW_MODULE_BUILD en include/ow/Shm.h):
    # el exe del kernel debe exportar los símbolos (en Linux lo cubre -rdynamic).
    target_link_options(owear PRIVATE
        "/EXPORT:ow_shm_put" "/EXPORT:ow_shm_data" "/EXPORT:ow_shm_shutdown")
    # Pila de 8 MiB (default 1 MiB desborda en la init de WebView2/Chromium —
    # misma recomendación /STACK:0x800000 de CEF; mordido con 0xC00000FD).
    target_link_options(owear PRIVATE "/STACK:0x800000")
elseif(NOT WIN32)
    target_link_options(owear PRIVATE -rdynamic) # módulos .owm resuelven ow_shm_* del host
endif()
if(OW_PLATFORM STREQUAL "linux")
    target_compile_options(owear PRIVATE -Wall -Wextra)
endif()
