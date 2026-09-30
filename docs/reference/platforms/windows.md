---
title: Windows
description: With the precompiled runtime (@owear/win32-x64) you do not need
order: 3
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Windows

## Running without building anything

With the precompiled runtime (`@owear/win32-x64`) you do **not** need
Visual Studio, CMake, vcpkg, or OpenSSL. You only need:

- **Node.js LTS**
- **Git** (if you clone the repo)
- the **WebView2 Runtime** (bundled in Windows 11; on Windows 10 it usually comes
  with an up-to-date Edge)

Install them with `winget` via the helper script:

```powershell
powershell -ExecutionPolicy Bypass -File tools\windows\setup.ps1
```

This installs Node + Git + WebView2 as needed, and the CLI (`npm i -g @owear/cli`).

> **OpenSSL vs OpenSSH** — do not confuse them. *OpenSSL* is only needed to build
> the kernel from source (it is inside vcpkg for Windows). *OpenSSH* is Windows'
> SSH server, useful only for remote command access.

## Create and run an app

```powershell
ow create my-app
cd my-app
npm install
npm run dev
```

`ow dev` detects `@owear/win32-x64` (an optional dependency) and does not compile
the kernel. A WebView2 window opens.

## Building from source (contributors)

Install MSVC (Visual Studio 2022), vcpkg (`zlib`, `openssl`), and the WebView2 SDK
under `deps/webview2/`. WebView2 support is gated behind `OW_WITH_WEBVIEW2`
(default `OFF`): without the SDK the build produces a stub
`CreateWebviewBackend() → nullptr` with a clear log. Enable it by installing the
`Microsoft.Web.WebView2` NuGet package.

```bat
cmake --preset windows-release
cmake --build --preset windows-release
```

## Remote access over SSH (optional)

Useful when another machine drives the build:

```powershell
# install and start the server (admin)
powershell -ExecutionPolicy Bypass -File tools\windows\setup.ps1 -OpenSSH
```

For an **administrator** user, the public key goes in
`C:\ProgramData\ssh\administrators_authorized_keys` (not `~/.ssh/authorized_keys`)
with restricted ACLs:

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r
icacls C:\ProgramData\ssh\administrators_authorized_keys /grant 'SYSTEM:(R)' /grant 'BUILTIN\Administrators:(R)'
Restart-Service sshd
```

For a non-admin user: `C:\Users\<user>\.ssh\authorized_keys`.

## Notes

- **Session 0:** an SSH session has no desktop, so windowed apps must be launched
  from your interactive session. You can build and create files over SSH, but run
  the window yourself.
- **Remote web debugging (optional):** set
  `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222` and connect
  Playwright with `connectOverCDP` to capture screenshots without a visible
  desktop.
- Native modules (`.owm`) compile **without** WebView2.
- Platform features (screen capture, global shortcuts, tray, notifications,
  power) use Win32 APIs; some are marked `VERIFY ON REAL DESKTOP`.
