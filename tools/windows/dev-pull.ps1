# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tools/windows/dev-pull.ps1 — descarga la compilación cross (servida por HTTP
# desde el Linux) y la deja en C:\owear-dev\deps\win32-x64.
#
#   powershell -ExecutionPolicy Bypass -File tools\windows\dev-pull.ps1 -LinuxHost 192.168.10.49
#
param(
    [string]$LinuxHost = "192.168.10.49",
    [int]$Port = 8000,
    [string]$Dest = "C:\owear-dev\deps\win32-x64"
)

$ErrorActionPreference = "Stop"
$zip = Join-Path $env:TEMP "win-out.zip"

Write-Host "[dev-pull] descargando http://$LinuxHost`:$Port/win-out.zip ..." -ForegroundColor Cyan
Invoke-WebRequest "http://$LinuxHost`:$Port/win-out.zip" -OutFile $zip

if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
New-Item -ItemType Directory -Force $Dest | Out-Null
Expand-Archive -Force $zip -DestinationPath $Dest
Remove-Item $zip -Force

Write-Host "[dev-pull] listo en $Dest" -ForegroundColor Green
Write-Host "  kernel: $Dest\bin\owear.exe"
Write-Host "  usa:    `$env:OW_KERNEL_BIN='$Dest\bin\owear.exe'  y luego  ow dev"
