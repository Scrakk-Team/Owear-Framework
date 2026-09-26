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
# clang-cl = clang en modo MSVC. El STL de MSVC 14.44 exige Clang 19+.
find_program(OW_CLANG NAMES clang-cl-19 clang-cl-20 clang-cl NAMES_PER_DIR REQUIRED)
set(CMAKE_C_COMPILER   "${OW_CLANG}")
set(CMAKE_CXX_COMPILER "${OW_CLANG}")
set(CMAKE_C_COMPILER_FRONTEND_VARIANT   MSVC)
set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT MSVC)
set(CMAKE_C_COMPILER_TARGET   x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# CRT estático (/MT): autocontenido (sin vcruntime DLL) y evita el debug CRT
# (xwin no incluye msvcrtd.lib).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

find_program(OW_LLD_LINK NAMES lld-link-19 lld-link-20 lld-link NAMES_PER_DIR REQUIRED)
set(CMAKE_LINKER "${OW_LLD_LINK}")
# En modo MSVC CMake invoca el archivador con /out: → llvm-lib (no llvm-ar).
find_program(OW_LLVM_LIB NAMES llvm-lib-19 llvm-lib-20 llvm-lib NAMES_PER_DIR REQUIRED)
set(CMAKE_AR "${OW_LLVM_LIB}")
find_program(OW_LLVM_RANLIB NAMES llvm-ranlib-19 llvm-ranlib NAMES_PER_DIR)
if(OW_LLVM_RANLIB)
    set(CMAKE_RANLIB "${OW_LLVM_RANLIB}")
endif()
find_program(OW_LLVM_RC NAMES llvm-rc-19 llvm-rc NAMES_PER_DIR REQUIRED)
set(CMAKE_RC_COMPILER "${OW_LLVM_RC}")
# mt de Microsoft no existe en Linux: llvm-mt hace lo mismo.
find_program(OW_LLVM_MT NAMES llvm-mt-19 llvm-mt NAMES_PER_DIR)
if(OW_LLVM_MT)
    set(CMAKE_MT "${OW_LLVM_MT}")
endif()

# ── Windows SDK / MSVC CRT (xwin) ───────────────────────────────────────────
if(NOT DEFINED OW_WIN_SYSROOT)
    set(OW_WIN_SYSROOT "$ENV{HOME}/win-sdk")
endif()
# Includes para el compilador (clang-cl entiende /winsysroot).
set(CMAKE_C_FLAGS_INIT   "/winsysroot ${OW_WIN_SYSROOT}")
set(CMAKE_CXX_FLAGS_INIT "/winsysroot ${OW_WIN_SYSROOT}")

# lld-link se invoca DIRECTO (no vía clang-cl), así que hay que darle las
# libpath del SDK explícitamente (no entiende /winsysroot).
# "Windows Kits" tiene un espacio que rompe los flags /libpath → symlink sin espacios.
set(OW_WINKITS "${OW_WIN_SYSROOT}/winkits")
if(NOT EXISTS "${OW_WINKITS}")
    file(CREATE_LINK "${OW_WIN_SYSROOT}/Windows Kits" "${OW_WINKITS}" SYMBOLIC)
endif()
file(GLOB OW_MSVC_DIRS "${OW_WIN_SYSROOT}/VC/Tools/MSVC/*")
file(GLOB OW_SDK_LIB_DIRS "${OW_WINKITS}/10/Lib/*")
list(SORT OW_MSVC_DIRS)
list(SORT OW_SDK_LIB_DIRS)
if(OW_MSVC_DIRS)
    list(GET OW_MSVC_DIRS -1 OW_MSVC_DIR)
endif()
if(OW_SDK_LIB_DIRS)
    list(GET OW_SDK_LIB_DIRS -1 OW_SDK_LIB_DIR)
endif()

set(OW_LINK_LIBS "")
if(OW_MSVC_DIR)
    string(APPEND OW_LINK_LIBS "/libpath:${OW_MSVC_DIR}/lib/x64 ")
endif()
if(OW_SDK_LIB_DIR)
    string(APPEND OW_LINK_LIBS
        "/libpath:${OW_SDK_LIB_DIR}/ucrt/x64 "
        "/libpath:${OW_SDK_LIB_DIR}/um/x64 ")
endif()
# OJO: string con espacios (no lista) — CMake une listas con ';' y vs_link_exe
# lo rompe. Separados por espacio se pasan como args distintos.
string(STRIP "${OW_LINK_LIBS}" OW_LINK_LIBS)
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${OW_LINK_LIBS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${OW_LINK_LIBS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${OW_LINK_LIBS}")

# ── deps cross (zlib + OpenSSL) ─────────────────────────────────────────────
if(NOT DEFINED OW_WIN_DEPS)
    set(OW_WIN_DEPS "$ENV{HOME}/win-deps")
endif()
list(APPEND CMAKE_PREFIX_PATH "${OW_WIN_DEPS}")
set(CMAKE_FIND_ROOT_PATH "${OW_WIN_DEPS}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
