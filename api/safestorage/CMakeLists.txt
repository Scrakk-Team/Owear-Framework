# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# Linux: AES-256-GCM vía OpenSSL (la clave vive en el data dir de la app, 0600).
# Windows: DPAPI (crypt32). macOS: stub (fuera de 0.1.x).
if(OW_PLATFORM STREQUAL "linux")
    if(NOT OW_WITH_OPENSSL)
        message(FATAL_ERROR "safestorage en Linux requiere OpenSSL (OW_WITH_OPENSSL=ON)")
    endif()
    find_package(OpenSSL REQUIRED)
    set(OW_SS_LIBS OpenSSL::Crypto)
elseif(OW_PLATFORM STREQUAL "win")
    set(OW_SS_LIBS crypt32)
elseif(OW_PLATFORM STREQUAL "mac")
    set(OW_SS_LIBS "-framework CoreFoundation" "-framework Security")
endif()
ow_add_module(safestorage SOURCES src/safestorage_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${OW_SS_LIBS})
