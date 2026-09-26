# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tools/windows/setup.ps1 — instala lo MÍNIMO para correr Owear en Windows.
# NO compila nada: usa el runtime precompilado @owear/win32-x64.
#
#   powershell -ExecutionPolicy Bypass -File tools\windows\setup.ps1
#   powershell -ExecutionPolicy Bypass -File tools\windows\setup.ps1 -OpenSSH
#
# Requiere winget (Windows 10 21H2+ / Windows 11).
param(
    [switch]$OpenSSH
)

$ErrorActionPreference = 'Continue'

function Have($cmd) { return [bool](Get-Command $cmd -ErrorAction SilentlyContinue) }
function Info($m) { Write-Host "[setup] $m" -ForegroundColor Cyan }
function Ok($m) { Write-Host "[setup] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "[setup] $m" -ForegroundColor Yellow }

# ── Node.js ──────────────────────────────────────────────────────────────────
if (Have node) {
    Ok "Node.js ya instalado: $(node --version)"
} else {
    Info "instalando Node.js LTS…"
    winget install --id OpenJS.NodeJS.LTS -e --accept-source-agreements --accept-package-agreements
}

# ── Git ──────────────────────────────────────────────────────────────────────
if (Have git) {
    Ok "Git ya instalado: $(git --version)"
} else {
    Info "instalando Git…"
    winget install --id Git.Git -e --accept-source-agreements --accept-package-agreements
}

# ── WebView2 Runtime (lo necesita el kernel) ─────────────────────────────────
$wv2 = Get-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}' -ErrorAction SilentlyContinue
if ($wv2) {
    Ok "WebView2 Runtime ya instalado: $($wv2.pv)"
} else {
    Info "instalando WebView2 Runtime…"
    winget install --id Microsoft.EdgeWebView2Runtime -e --accept-source-agreements --accept-package-agreements
}

# ── CLI de Owear (trae el runtime Windows como dependencia opcional) ─────────
if (Have npm) {
    Info "instalando @owear/cli…"
    npm install -g @owear/cli
    if (Have ow) { Ok "CLI listo: $(ow --version 2>$null)" }
} else {
    Warn "npm no está en el PATH todavía — cierra y reabre la terminal y ejecuta: npm install -g @owear/cli"
}

# ── OpenSSH Server (acceso remoto por comandos) ─────────────────────────────
if ($OpenSSH) {
    Info "instalando OpenSSH Server…"
    Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0 | Out-Null
    Start-Service sshd
    Set-Service -Name sshd -StartupType Automatic
    if (-not (Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -DisplayName 'OpenSSH Server (sshd)' `
            -Enabled True -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22 | Out-Null
    }
    Ok "sshd activo. IP(s): $((Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike '169.*' }).IPAddress -join ', ')"
    Warn "Auth por clave: copia tu .pub a C:\ProgramData\ssh\administrators_authorized_keys (admin) o ~/.ssh/authorized_keys"
}

Ok "listo. Siguiente: ow create mi-app ; cd mi-app ; npm install ; npm run dev"
