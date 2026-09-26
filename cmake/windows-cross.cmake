# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# cmake/windows-cross.cmake — toolchain para compilar el kernel de Windows x64
# (ABI MSVC) DESDE Linux, con clang-cl + lld-link y el MSVC/Windows SDK
# descargado por `xwin`. Sin Visual Studio.
#
#   cmake --preset windows-cross
#
# Variables (con defaults):
#   OW_WIN_SYSROOT  dir del SDK de xwin      (default: $HOME/win-sdk)
#   OW_WIN_DEPS     zlib+OpenSSL cross       (default: $HOME/win-deps)
#
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# ── compiladores (MSVC ABI) ──────────────────────────────────────────────────
# clang-cl = clang en modo MSVC. CMake lo detecta con el frontend variant.
find_program(OW_CLANG NAMES clang-cl clang REQUIRED)
set(CMAKE_C_COMPILER   "${OW_CLANG}")
set(CMAKE_CXX_COMPILER "${OW_CLANG}")
set(CMAKE_C_COMPILER_FRONTEND_VARIANT   MSVC)
set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT MSVC)
set(CMAKE_C_COMPILER_TARGET   x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

find_program(OW_LLD_LINK NAMES lld-link REQUIRED)
set(CMAKE_LINKER "${OW_LLD_LINK}")
find_program(OW_LLVM_AR NAMES llvm-ar REQUIRED)
set(CMAKE_AR "${OW_LLVM_AR}")
find_program(OW_LLVM_RANLIB NAMES llvm-ranlib REQUIRED)
set(CMAKE_RANLIB "${OW_LLVM_RANLIB}")
find_program(OW_LLVM_RC NAMES llvm-rc REQUIRED)
set(CMAKE_RC_COMPILER "${OW_LLVM_RC}")

# ── Windows SDK / MSVC CRT (xwin) ───────────────────────────────────────────
if(NOT DEFINED OW_WIN_SYSROOT)
    set(OW_WIN_SYSROOT "$ENV{HOME}/win-sdk")
endif()
set(CMAKE_C_FLAGS_INIT   "/winsysroot ${OW_WIN_SYSROOT}")
set(CMAKE_CXX_FLAGS_INIT "/winsysroot ${OW_WIN_SYSROOT}")

# ── deps cross (zlib + OpenSSL) ─────────────────────────────────────────────
if(NOT DEFINED OW_WIN_DEPS)
    set(OW_WIN_DEPS "$ENV{HOME}/win-deps")
endif()
list(APPEND CMAKE_PREFIX_PATH "${OW_WIN_DEPS}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
