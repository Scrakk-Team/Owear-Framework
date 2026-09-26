#!/usr/bin/env bash
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tools/windows-cross/serve.sh — empaqueta la salida del cross-build ($OW_WIN_OUT)
# en win-out.zip y la sirve por HTTP para que Windows la descargue.
#
#   bash tools/windows-cross/serve.sh [puerto]   # default 8000
#
set -euo pipefail
OUT="${OW_WIN_OUT:-$HOME/win-out}"
PORT="${1:-8000}"

[ -d "$OUT" ] || { echo "no existe $OUT — corre tools/windows-cross/build.sh"; exit 1; }
[ -f "$OUT/bin/owear.exe" ] || { echo "$OUT/bin/owear.exe no encontrado"; exit 1; }

cd "$(dirname "$OUT")"
rm -f win-out.zip
( cd "$OUT" && python3 -c "import shutil; shutil.make_archive('../win-out','zip','.')" )

IP="$(hostname -I | awk '{print $1}')"
echo "win-out.zip listo ($(du -h win-out.zip | cut -f1))"
echo "en Windows (PowerShell):"
echo "  iwr http://$IP:$PORT/win-out.zip -OutFile \$env:TEMP\\win-out.zip"
echo "  Expand-Archive -Force \$env:TEMP\\win-out.zip C:\\owear-dev\\deps\\win32-x64"
echo
echo "sirviendo en http://$IP:$PORT/  (Ctrl+C para parar)"
python3 -m http.server "$PORT" --directory "$(dirname "$OUT")"
