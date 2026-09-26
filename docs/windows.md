<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Owear en Windows

Guía mínima para **correr** Owear en Windows sin compilar nada, y para montar el
acceso remoto por comandos.

## OpenSSL vs OpenSSH (no los confundas)

- **OpenSSL** solo hace falta para **compilar el kernel desde fuentes** (Linux lo
  enlaza; en Windows va dentro de vcpkg). **Con el runtime precompilado
  `@owear/win32-x64` NO lo necesitas.** Por eso no instalas VS/CMake/vcpkg.
- **OpenSSH** es el **servidor SSH** de Windows, para que otra máquina (tu Linux)
  ejecute comandos aquí. Es opcional, solo para el flujo remoto.

## Instalar SOLO lo necesario

Necesitas: **Node.js LTS**, **Git** (si vas a clonar el repo) y el **WebView2
Runtime** (ya viene en Windows 11; en Win10 suele estar con Edge actualizado).

Con un comando (usa `winget`):

```powershell
powershell -ExecutionPolicy Bypass -File tools\windows\setup.ps1
```

Eso instala Node + Git + WebView2 (si faltan) y el CLI (`npm i -g @owear/cli`).
**Nada más.**

## Crear y correr una app

```powershell
ow create mi-app
cd mi-app
npm install
npm run dev
```

`ow dev` detecta `@owear/win32-x64` (dependencia opcional) y **no compila** el
kernel. Se abre una ventana WebView2.

## Acceso remoto por SSH (para que el agente ejecute comandos)

```powershell
# 1) instala y arranca el servidor (admin)
powershell -ExecutionPolicy Bypass -File tools\windows\setup.ps1 -OpenSSH

# 2) desde Linux, copia tu clave pública. Para un usuario ADMINISTRADOR:
#    la clave va en C:\ProgramData\ssh\administrators_authorized_keys
#    (¡no en ~/.ssh/authorized_keys!) y con ACL restringidos:
```

En Windows (admin), tras pegar la clave en `C:\ProgramData\ssh\administrators_authorized_keys`:

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r
icacls C:\ProgramData\ssh\administrators_authorized_keys /grant 'SYSTEM:(R)' /grant 'BUILTIN\Administrators:(R)'
Restart-Service sshd
```

Para un usuario **no admin**, la clave va en `C:\Users\<usuario>\.ssh\authorized_keys`.

Desde Linux: `ssh usuario@IP-windows` (o por **Tailscale** si no estáis en LAN).

## Notas

- **Sesión 0**: una sesión SSH en Windows no tiene escritorio, así que las apps
  con ventana deben lanzarse en tu **sesión interactiva** (tú abres la terminal y
  corres `npm run dev`). El agente puede compilar/crear ficheros por SSH; tú
  ejecutas y ves la ventana.
- **Debug remoto de la web (opcional)**: con
  `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222`, Playwright
  puede conectarse al WebView2 desde otra máquina (`connectOverCDP`) y hacer
  capturas sin ver el escritorio.
