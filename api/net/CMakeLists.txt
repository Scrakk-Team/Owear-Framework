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
        ../../src/Runtime/Http.cpp
        ../../src/Runtime/Sha256.cpp
    LIBS OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB Threads::Threads
    INCLUDES ${CMAKE_SOURCE_DIR}/src)
