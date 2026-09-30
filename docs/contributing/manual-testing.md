---
title: Manual testing in the starter
description: examples/starter is the reference app used to exercise every subsystem by hand.
order: 3
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Manual testing in the starter

`examples/starter` is the reference app used to exercise every subsystem by hand.
This page tracks how to run it and what to check. It is a living document.

Status legend:

- ✅ **Verified** — tested end to end on the indicated platform.
- 🟡 **Partial** — implemented; UI panel in the starter pending.
- ⏳ **Pending** — not implemented or not yet tested in the starter.

## Running the starter

```bash
# Linux
cd examples/starter
npm run dev
```

On **Windows** (cross-built kernel pulled over HTTP):

```powershell
taskkill /IM owear.exe /F 2>$null
$dst = "$env:USERPROFILE\owear-dev\deps\win32-x64"
Remove-Item -Recurse -Force $dst -ErrorAction SilentlyContinue
iwr http://<linux-host>:8000/win-out.zip -OutFile $env:TEMP\win-out.zip
Expand-Archive -Force $env:TEMP\win-out.zip $dst
$env:OW_KERNEL_BIN = "$dst\bin\owear.exe"
cd <path-to-starter>
npm.cmd run dev
```

## Block A — Execution / IPC

| System | Demonstrates | Linux | Windows |
|---|---|---|---|
| `app.forkWorker` | Node worker with a channel (replaces `utilityProcess.fork`) | ✅ | 🟡 |
| `windowId` in handlers | `app.handleContext((ctx) => ctx.windowId)` | ✅ | 🟡 |
| `webContents.send` | directed `win.webContents.send(name, payload)` | ✅ | 🟡 |
| `MessageChannel` | `app.createChannel()` + `ow.port` (main ↔ renderer) | ✅ | 🟡 |
| N-API | native addons | ✅ (docs) | ⏳ |

```ts
// worker
const w = app.forkWorker('echo-worker.js')
w.on('message', (m) => console.log('worker →', m))
w.postMessage({ ping: 1 })
// app/workers/echo-worker.ts (Electron style)
process.parentPort.on('message', (e) => process.parentPort.postMessage({ echo: e.data }))

// context handler
app.handleContext('whoami', (ctx) => ({ windowId: ctx.windowId }))
```

The CLI compiles `app/workers/**` → `.owear/workers/**` and exposes
`OW_APP_WORKERS`.

## Block B — Protocol / data / OS

| System | Demonstrates | Linux | Windows |
|---|---|---|---|
| `app.protocol` (handler) | scheme served by the main process | ✅ | 🟡 |
| `app.protocol` (serve) | kernel serves a directory | ✅ | 🟡 |
| `safeStorage` | OS-backed encryption | ✅ | 🟡 |
| `theme` | light/dark + force + event | ✅ | 🟡 |
| `session` permissions | geolocation/notifications/… | ✅ | 🟡 |
| `session` partitions | `session.fromPartition` | ✅ | 🟡 |
| `session` webRequest | `onBeforeRequest` (cancel/redirect) | ✅ (navigations) | 🟡 (all requests) |

```ts
await app.protocol('scrakk-ext', {
  privileged: { secure: true, cors: true },
  handler: async () => new Response('<h1>hi</h1>', { headers: { 'content-type': 'text/html' } }),
})
await app.protocol('assets', { serve: '/path/to/dir' })

const { data, encrypted } = await safeStorage.encrypt('token')
const text = await safeStorage.decrypt(data)

await theme.setSource('dark')
theme.watch()   // ow.on('theme.changed', …)

session.onPermissionRequest(({ permission, origin }) => permission === 'geolocation')

webRequest.onBeforeRequest({ urls: ['*://example.com/*'] }, () => ({ cancel: true }))
```

## Block C — App shell

### C1 — `app`

Verified on Linux (paths, `setPath`, identity, `commandLine`, lifecycle events).
On Windows, `userData` should be `%LOCALAPPDATA%\<OW_APP_ID>` and `exe` the
`owear.exe` folder.

```ts
app.on('window-all-closed', () => console.log('window-all-closed'))
for (const n of ['home', 'userData', 'temp', 'logs', 'downloads', 'exe', 'appPath'])
  console.log('path', n, '=', app.getPath(n))
app.commandLine.appendSwitch('use-gl', 'angle')
```

### C2 — `dialog`

Verified on Linux (modal dialogs, not automatable). Windows compiles
(`IFileDialog` + `TaskDialogIndirect`).

```ts
await ow.invoke('dialog', 'showOpenDialog', { properties: ['openFile', 'multiSelections'] })
await ow.invoke('dialog', 'showSaveDialog', { defaultPath: 'x.txt' })
await ow.invoke('dialog', 'showMessageBox', { type: 'question', message: 'Continue?', buttons: ['Yes', 'No'] })
```

### C3 — `webContents`

Verified on Linux (`did-finish-load`, `capturePage`, `getURL`, `send`).

```ts
const wc = win.webContents
wc.on('did-finish-load', () => {})
const img = await wc.capturePage()
wc.setWindowOpenHandler(() => ({ action: 'deny' }))
```

### C4 — `nativeImage`

Verified on Linux (SDK tests) and Windows (pure Node).

```ts
const img = nativeImage.createFromPath('icon.png')
img.resize({ width: 16 }).toPNG()
img.toDataURL()
```

### C5 — `Menu`

Verified on Linux (GTK popup with roles/checkbox/radio); the menubar is a no-op
by design. Accelerators are display-only in v1.

### C6 — `Tray`

On Linux, the kernel uses native StatusNotifierItem. Modern GNOME needs the
appindicator extension; see [Linux platform notes](../reference/platforms/linux.md).

### C7 — `theme`

Linux: read + events ✅; forcing is limited (WebKitGTK). Windows: forcing works
via WebView2 `PreferredColorScheme`.

### C8 — `print` / `printToPDF`

Linux supports `print` (dialog) and `printToPDF` (rasterized via Cairo). Windows
compiles real `print` + `printToPDF`.

### C9 — `BrowserWindow`

Verified on Linux (state, onTop, progress, min/max, `getAllWindows`,
`getFocusedWindow`, `fromId`).

### C10 — `screen` and `powerMonitor`

Verified on Linux (displays/cursor/nearest/matching, idle/state/battery, watch,
blockers, X11/GDK events).

Manual event check: connect/disconnect a monitor or change its resolution → a
`screen.added/removed/changed` line should appear. Suspend the machine or switch
AC/battery → `power.*`. On Windows, locking the session → `power.lock`/`unlock`.

## Still to document here

- Concrete starter buttons/panels per subsystem.
- Session partitions and `webRequest`.
- Windows testing of everything marked 🟡 (once hardware is available).
