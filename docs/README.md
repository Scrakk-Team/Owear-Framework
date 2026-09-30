---
title: Owear documentation
description: Owear is a native, multiplatform desktop framework. It does not bundle a
order: 1
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Owear documentation

Owear is a native desktop framework for **Linux and Windows**. It does **not**
bundle a browser: it embeds the WebView that already ships with the operating
system (WebView2 on Windows, WebKitGTK on Linux). Your UI is web code;
everything around it is a small native kernel written in C++.

This folder is the complete reference. Start with **Getting started** if you are
building an app, and with **Architecture** if you are contributing to the
framework itself.

---

## Getting started

| Document | What it covers |
|---|---|
| [Installation](getting-started/installation.md) | Requirements, installing the CLI, first build |
| [Quick start](getting-started/quickstart.md) | Create a project and run it in 2 minutes |
| [Project structure](getting-started/project-structure.md) | What every generated file/folder is for |
| [Your first app](getting-started/your-first-app.md) | A full tutorial: a small notes app end to end |
| [Dev and build](getting-started/dev-and-build.md) | How `ow dev` and `ow build` work under the hood |

## Guides (task oriented)

| Document | What it covers |
|---|---|
| [Windows](guides/windows.md) | `BrowserWindow`, options, lifecycle, custom title bars |
| [Renderer API](guides/renderer-api.md) | `window.ow`: calling native code from the UI |
| [IPC](guides/ipc.md) | Renderer ↔ main process, `app.send`, ports, events |
| [Main process](guides/main-process.md) | The Node sidecar, `app`, workers, lifecycle |
| [Native modules](guides/native-modules.md) | Write C++ `.owm` modules and call them from TS |
| [Filesystem](guides/filesystem.md) | `fs` and `path`: files, large files, watchers |
| [Menus and tray](guides/menus-and-tray.md) | Application menus, context menus, tray icons |
| [Dialogs and notifications](guides/dialogs-and-notifications.md) | Native dialogs and system notifications |
| [Clipboard and shell](guides/clipboard-and-shell.md) | Clipboard (text/image), open URLs/paths |
| [Sessions and downloads](guides/sessions-and-downloads.md) | Cookies, cache, proxy, downloads, permissions |
| [Embedded webviews](guides/embedded-webviews.md) | Child webviews inside a window |
| [Theme](guides/theme.md) | Light/dark detection, forcing, `theme.changed` |
| [Screen and power](guides/screen-and-power.md) | Multi-monitor, cursor, idle, suspend, blockers |
| [Global shortcuts](guides/global-shortcuts.md) | System-wide keyboard shortcuts |
| [Processes and PTY](guides/process-and-pty.md) | Spawning processes and interactive terminals |
| [Networking](guides/networking.md) | Native HTTP(S) without CORS, downloads |
| [Safe storage](guides/safe-storage.md) | OS-backed encryption for secrets |
| [Security](guides/security.md) | Permissions, protocol isolation, hardening |
| [Debugging](guides/debugging.md) | DevTools, logs, crash reports, common errors |
| [Packaging](guides/packaging.md) | `ow build app`: binary, deb, AppImage, MSI |
| [Installers](guides/installers.md) | Installer/uninstaller apps, payloads, shortcuts |
| [Auto-update](guides/auto-update.md) | Delta updates, signing, rollback, boot guard |
| [Native addons](guides/native-addons.md) | N-API addons and Node compatibility |
| [Migrate from Electron](guides/migration-from-electron.md) | Equivalences and porting notes |

## API reference

| Document | What it covers |
|---|---|
| [Renderer `window.ow`](api/renderer.md) | The object the kernel injects in every document |
| [SDK `@owear/core`](api/sdk/README.md) | Every export of the main-process SDK |
| [Native modules](api/modules/README.md) | One page per `api/<name>/` module |

## Reference

| Document | What it covers |
|---|---|
| [CLI](reference/cli.md) | Every `ow` command and flag |
| [Control protocol](reference/control-protocol.md) | NDJSON commands between SDK and kernel |
| [Environment variables](reference/environment-variables.md) | Every `OW_*` variable |
| [C++ API](reference/cpp-api.md) | Public headers in `include/ow/` |
| [Module manifests](reference/manifests.md) | `owear.module.json` and code generation |
| [Platforms](reference/platforms/linux.md) | Platform-specific notes ([Linux](reference/platforms/linux.md) · [Windows](reference/platforms/windows.md)) |

## Architecture and contributing

| Document | What it covers |
|---|---|
| [Architecture overview](architecture/overview.md) | Processes, bridge, module system |
| [Kernel and lifecycle](architecture/kernel.md) | App bootstrap, main loop, window manager |
| [Bridge](architecture/bridge.md) | Renderer ↔ kernel transport and wire format |
| [Testing](architecture/testing.md) | Unit and E2E test suites |
| [Contributing setup](contributing/setup.md) | Build, test, and submit changes |
| [Architecture rules](contributing/architecture-rules.md) | The invariants that keep both platforms working |
| [Adding an API](contributing/adding-an-api.md) | The full walkthrough to add a native module |
| [Manual testing](contributing/manual-testing.md) | Exercising every subsystem in the starter app |

## Project

| Document | What it covers |
|---|---|
| [Roadmap](roadmap.md) | Feature phases, per-API status, CI matrix |
| [Changelog](changelog/README.md) | Release-by-release history |

---

## At a glance

```text
┌─────────────────────────────────────────────┐
│  App process (native, ~5-10 MB)             │
│  kernel: app · windows · bridge · loader    │
│  .owm modules: fs · dialog · yours          │
└──────────────┬──────────────────────────────┘
               │ direct bridge (no Node in between)
     OS WebView  ←  your frontend (Vite/React/…)
               │
     Node sidecar (auto-installed, optional)
       └── app/main.ts — Electron-like API
```

- **Kernel** — the native binary (`owear`). Owns windows, the WebView, the
  bridge, and loads `.owm` modules on demand.
- **Renderer** — your web frontend. It talks to the kernel through `window.ow`.
- **Main process** — an optional Node process (`app/main.ts`) for things that
  need Node: filesystem glue, database drivers, jobs.
