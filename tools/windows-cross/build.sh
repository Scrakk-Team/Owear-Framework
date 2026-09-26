#!/usr/bin/env bash
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tools/windows-cross/build.sh — compila el kernel de Windows x64 DESDE Linux
# (clang-cl + lld-link + SDK de xwin) y deja el resultado listo para copiar a
# Windows en $OW_WIN_OUT.
#
#   bash tools/windows-cross/build.sh
#
# Variables:
#   OW_WIN_SYSROOT  SDK de xwin        (default: ~/win-sdk)
#   OW_WIN_DEPS     zlib+OpenSSL cross (default: ~/win-deps)
#   OW_WIN_OUT      salida             (default: ~/win-out)
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SYSROOT="${OW_WIN_SYSROOT:-$HOME/win-sdk}"
DEPS="${OW_WIN_DEPS:-$HOME/win-deps}"
OUT="${OW_WIN_OUT:-$HOME/win-out}"

if [ ! -d "$SYSROOT" ]; then
    echo "Falta el SDK en $SYSROOT."
    echo "  xwin --accept-license splat --output $SYSROOT --use-winsysroot-style"
    exit 1
fi
if [ ! -d "$DEPS" ]; then
    echo "Faltan deps cross (zlib/OpenSSL) en $DEPS — corre tools/windows-cross/build-deps.sh"
    exit 1
fi

export OW_WIN_SYSROOT="$SYSROOT"
export OW_WIN_DEPS="$DEPS"

cmake --preset windows-cross
cmake --build --preset windows-cross

BUILD="$ROOT/build/windows-cross"
BIN="$BUILD/src/owear.exe"
[ -f "$BIN" ] || { echo "no se generó $BIN"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/bin/modules"
cp "$BIN" "$OUT/bin/"

# módulos (.dll) — Ninja: api/<x>/<x>.dll ; MSVC multi-config: api/<x>/Release/<x>.dll
find "$BUILD/api" -type f -name '*.dll' -exec cp {} "$OUT/bin/modules/" \;

# headers públicos (para owear-build-native)
cp -r "$ROOT/include" "$OUT/include"

# DLLs de soporte junto al exe (si zlib/OpenSSL no son estáticos)
find "$BUILD" -type f -name '*.dll' ! -path '*/api/*' -exec cp -n {} "$OUT/bin/" \; 2>/dev/null || true

echo "→ $OUT  (bin/owear.exe + bin/modules/*.dll + include/)"
echo "   copia a Windows y usa:  OW_KERNEL_BIN=<...>\\bin\\owear.exe"
