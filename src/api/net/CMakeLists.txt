# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
if(NOT OW_WITH_OPENSSL)
    return()
endif()
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
ow_add_module(net
    SOURCES
        src/net.cpp
        ${CMAKE_SOURCE_DIR}/src/Runtime/Http.cpp
        ${CMAKE_SOURCE_DIR}/src/Runtime/Http/Url.cpp
        ${CMAKE_SOURCE_DIR}/src/Runtime/Http/Transport.cpp
        ${CMAKE_SOURCE_DIR}/src/Runtime/Http/Transport/Shims.cpp
        ${CMAKE_SOURCE_DIR}/src/Runtime/Http/Transport/Tls.cpp
        ${CMAKE_SOURCE_DIR}/src/Runtime/Http/Transport/Plain.cpp
        ${CMAKE_SOURCE_DIR}/src/Runtime/Sha256.cpp
    LIBS OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB Threads::Threads
    INCLUDES ${CMAKE_SOURCE_DIR}/src)
