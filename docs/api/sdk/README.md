---
title: @owear/core SDK
description: @owear/core is the main-process SDK. It talks to the kernel over a control
order: 1
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `@owear/core` SDK

`@owear/core` is the main-process SDK. It talks to the kernel over a control
socket (NDJSON) and mirrors Electron's API where it can. Import it from
`app/main.ts` and any Node module of your app.

```ts
import {
  app, BrowserWindow, WebContents, Menu, MenuItem, Tray,
  dialog, session, webRequest, protocol, safeStorage, nativeTheme, theme,
  screen, powerMonitor, powerSaveBlocker, nativeImage, NativeImage,
  invokeNative, listNativeModules, nativeModuleInfo,
  autoUpdater, installer, defineBridge,
  forkWorker, resolveWorkerEntry, platform,
} from '@owear/core'
```

## Pages

| Export | Reference |
|---|---|
| `app` | [app.md](app.md) |
| `BrowserWindow` | [browser-window.md](browser-window.md) |
| `WebContents` | [web-contents.md](web-contents.md) |
| `Menu`, `MenuItem` | [menu.md](menu.md) |
| `Tray` | [tray.md](tray.md) |
| `dialog` | [dialog.md](dialog.md) |
| `session` | [session.md](session.md) |
| `webRequest` | [web-request.md](web-request.md) |
| `app.protocol` / `protocolHandlers` | [protocol.md](protocol.md) |
| `safeStorage` | [safe-storage.md](safe-storage.md) |
| `theme` / `nativeTheme` | [theme.md](theme.md) |
| `screen` | [screen.md](screen.md) |
| `powerMonitor`, `powerSaveBlocker` | [power-monitor.md](power-monitor.md) |
| `nativeImage`, `NativeImage` | [native-image.md](native-image.md) |
| `invokeNative`, `listNativeModules`, `nativeModuleInfo` | [native-modules.md](native-modules.md) |
| `app.handle` / `app.send` (node bridge) | [node-ipc.md](node-ipc.md) |
| `app.createChannel` / ports | [ports.md](ports.md) |
| `forkWorker` | [workers.md](workers.md) |
| `autoUpdater` | [auto-updater.md](auto-updater.md) |
| `installer` | [installer.md](installer.md) |
| `defineBridge` | [bridge.md](bridge.md) |
| `platform` | `os.platform()` — a string constant |

## The control channel

Everything in this SDK ultimately writes NDJSON to the kernel's control socket
and reads responses. The lower-level pieces are also exported for advanced use:

- `invokeNative(module, method, ...args)` — call any native module from the main
  process, without a window.
- `channel` (internal `ControlChannel`) — raw `call(cmd, params)` and event
  emitter. Reachable as `app.__channel`.

See the [control protocol](../../reference/control-protocol.md) for the command
list.
