---
title: Migrating from Electron
description: Owear deliberately mirrors Electron's main-process API, so most of the port is
order: 12
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Migrating from Electron

Owear deliberately mirrors Electron's main-process API, so most of the port is
mechanical. The biggest conceptual difference is in the renderer: there is no
preload/IPC hop, and you call native modules directly.

## Concept mapping

| Electron | Owear |
|---|---|
| `app.whenReady()` | `app.whenReady()` (same) |
| `BrowserWindow` | `BrowserWindow` (same shape) |
| `ipcMain.handle` / `ipcRenderer.invoke` | `app.handle` + `ow.invoke('node','call',{fn,args})` |
| `webContents.send` / `ipcRenderer.on` | `app.send` / `ow.on` |
| `contextBridge.exposeInMainWorld` + preload | `window.ow` injected by the kernel |
| `electron-builder` | `ow build app` / `ow build installer` |
| `electron-updater` | `autoUpdater` |
| `nativeImage` | `nativeImage` (same) |
| `Tray`, `Menu`, `dialog`, `session`, `safeStorage`, `nativeTheme`, `powerMonitor`, `powerSaveBlocker`, `screen` | same names in `@owear/core` |
| N-API addons (`electron-rebuild`) | N-API addons (no rebuild) |
| Bundled Chromium | the OS WebView |

## Renderer: no preload, no IPC

Electron pushes everything through a preload script and IPC. In Owear the kernel
injects `window.ow`, and most capabilities are native modules you call directly:

```ts
// Electron: ipcRenderer.invoke('read-file', path) → ipcMain.handle(...)
const text = await window.ow.invoke<string>('fs', 'readText', path)
```

Only reach for Node when you truly need it:

```ts
const rows = await window.ow.invoke('node', 'call', { fn: 'db.query', args: [sql] })
```

See [Renderer API](renderer-api.md) and [IPC](ipc.md).

## Main process

Main-process code ports almost unchanged:

```ts
// Electron and Owear
import { app, BrowserWindow, Menu, Tray, dialog, session, nativeTheme } from '@owear/core'
```

Differences to watch:

- `BrowserWindow` construction is async (the window is created by the kernel).
  Use `win.on('ready-to-show')` before relying on `win.id`.
- `webContents.send` exists, plus `app.send`.
- `Menu.setApplicationMenu` is a **no-op on Linux**; draw your own menubar.
- `dialog`/`printToPDF` support depends on the platform's WebView (Linux
  `printToPDF` is not available in WebKitGTK v2.52 and returns a clear error).

## Packaging

Replace `electron-builder`/`electron-updater`:

```bash
ow build app --format deb      # instead of electron-builder
ow build installer             # a real installer + uninstaller
ow update --file … --version … # instead of electron-updater's publish
```

## Native addons

Electron binaries must be rebuilt for Node; N-API addons need nothing. See
[Native addons](native-addons.md).

## Things that simply do not exist

- `require('electron')`, `ipcRenderer`, `ipcMain`, `contextBridge`, `preload`.
- `utilityProcess` — use `app.forkWorker` (which shims `process.parentPort`).
- `BrowserView` — use embedded webviews ([`webview`](embedded-webviews.md)).
- Chromium-specific flags and DevTools protocol parity.

## A porting recipe

1. Move `main`/`preload` code into `app/main.ts`; keep window/menu/tray there.
2. Replace `ipcMain.handle` with `app.handle`.
3. In the renderer, replace `ipcRenderer.invoke` calls with `ow.invoke` to the
   corresponding native module (`fs`, `dialog`, `clipboard`, …).
4. Rebuild native addons for Node if needed.
5. Swap the packager for `ow build` and, if you used updates, `autoUpdater`.

## Next steps

- [Renderer API](renderer-api.md), [Main process](main-process.md), [IPC](ipc.md).
- [Packaging](packaging.md), [Auto-update](auto-update.md).
