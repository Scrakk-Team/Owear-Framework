#!/usr/bin/env bash
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tools/windows-cross/build-deps.sh — cross-compila las dependencias nativas
# (zlib) para x86_64-pc-windows-msvc (estáticas, /MT) en $OW_WIN_DEPS.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SYSROOT="${OW_WIN_SYSROOT:-$HOME/win-sdk}"
DEPS="${OW_WIN_DEPS:-$HOME/win-deps}"
CACHE="${OW_WIN_DEPS_CACHE:-$HOME/.owear-win-deps}"
ZVER="${ZLIB_VERSION:-1.3.1}"

export PATH="/usr/lib/llvm-19/bin:$PATH"
CLANG_CL="$(command -v clang-cl-19 || command -v clang-cl)"
LLVM_LIB="$(command -v llvm-lib-19 || command -v llvm-lib)"
[ -n "$CLANG_CL" ] && [ -n "$LLVM_LIB" ] || { echo "faltan clang-cl/llvm-lib (LLVM 19)"; exit 1; }
[ -d "$SYSROOT" ] || { echo "falta el SDK en $SYSROOT"; exit 1; }

mkdir -p "$CACHE" "$DEPS/include" "$DEPS/lib"
SRC="$CACHE/zlib-$ZVER"
if [ ! -d "$SRC" ]; then
    echo "descargando zlib $ZVER…"
    curl -fsSL "https://zlib.net/fossils/zlib-$ZVER.tar.gz" -o "$CACHE/zlib.tar.gz"
    tar -C "$CACHE" -xf "$CACHE/zlib.tar.gz"
fi

OBJ="$CACHE/obj-zlib"; rm -rf "$OBJ"; mkdir -p "$OBJ"
cd "$SRC"
for f in adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil; do
    "$CLANG_CL" --target=x86_64-pc-windows-msvc /winsysroot "$SYSROOT" \
        /MT /O2 /nologo /c "$f.c" "/Fo$OBJ/$f.obj"
done
"$LLVM_LIB" /nologo /out:"$DEPS/lib/zlib.lib" "$OBJ"/*.obj
cp zlib.h zconf.h "$DEPS/include/"
echo "zlib → $DEPS/lib/zlib.lib (+ headers)"
