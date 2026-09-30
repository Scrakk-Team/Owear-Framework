---
title: Main process
description: The main process is an optional Node program (the "sidecar") that the kernel
order: 10
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Main process

The main process is an optional Node program (the "sidecar") that the kernel
spawns when `OW_APP_MAIN` is set. `ow dev` and `ow build` compile `app/main.ts`
and set that variable for you. Its SDK, `@owear/core`, is intentionally close to
Electron.

```ts
import { app, BrowserWindow, Menu, Tray } from '@owear/core'

app.whenReady().then(() => {
  const win = new BrowserWindow({ width: 1024, height: 700, url: 'app://index.html' })
  win.on('closed', () => app.quit())
})
```

## `app`

### Lifecycle and identity

```ts
await app.whenReady()                    // connects to the kernel control socket
app.quit(exitCode = 0)
app.info()                               // { pid, version, socket }
app.getName() / app.setName(name)
app.getVersion()                         // package.json version, else kernel version
app.isPackaged()                         // true in a packaged app
app.getAppPath()                         // root of the app bundle
```

### Paths (Electron conventions)

```ts
app.getPath(name)   // home, appData, userData, sessionData, cache, temp, logs,
                    // downloads, documents, desktop, pictures, music, videos,
                    // exe, appPath
app.setPath(name, value)
```

### Events

```ts
app.on('before-quit', () => {})
app.on('will-quit', () => {})
app.on('window-all-closed', () => {})
app.on('second-instance', (argv) => {})
app.on('activate', () => {})
app.on('child-process-gone', () => {})
```

### Single instance

```ts
const gotLock = await invokeNative<boolean>('app', 'requestSingleInstanceLock')
if (!gotLock) {
  app.quit()
} else {
  app.on('second-instance', (payload) => { /* focus your window */ })
}
```

The first instance binds a per-app socket; a second instance connects, forwards
its `argv`, and exits. The first instance receives `second-instance`.

### Exposing Node to the renderer

```ts
app.handle(fn, handler)          // (…args) => result
app.handleContext(fn, handler)   // (ctx, …args) => result, ctx = { windowId }
app.send(name, payload?, windowId?)
```

See [IPC](ipc.md).

### Browser flags

```ts
app.commandLine.appendSwitch('disable-gpu')
app.commandLine.appendArgument('--enable-features=Foo')
app.commandLine.hasSwitch('disable-gpu')
app.commandLine.getSwitchValue('disable-gpu')
```

Applied per WebView when windows are created (Windows: AdditionalBrowserArguments;
Linux: best effort).

### Custom protocols

```ts
app.protocol('scrakk-ext', {
  privileged: { secure: true, cors: true },
  handler: async (req) => new Response('<h1>hello</h1>', { headers: { 'content-type': 'text/html' } }),
})

app.protocol('assets', { serve: '/path/to/dir' })   // kernel serves the directory
```

The handler runs in the main process and may return a `Response`, a
`{ status, headers, body }` object, a string, or `null` (→ 404). See
[protocol reference](../api/sdk/protocol.md).

### Node runtime and workers

```ts
await app.ensureNodeRuntime('lts')   // { path, version, source }
app.forkWorker('heavy-worker.js')
app.workersDir()                     // OW_APP_WORKERS, if defined
```

`ensureNodeRuntime` resolves a Node binary with the priority
`OW_NODE_BIN → system Node → Owear cache → official download`. `source` tells you
which path won (`env | system | cache | downloaded`).

### App icon

```ts
app.setIcon('/path/to/icon.png')     // default icon for windows created afterwards
```

## Workers

Workers are child processes with an IPC channel — the replacement for Electron's
`utilityProcess.fork`.

```ts
const w = app.forkWorker('tree-sitter-worker.js')     // or app/workers/tree-sitter-worker.ts
w.on('message', (m) => console.log(m))
w.postMessage({ op: 'tokenize', text })
w.on('exit', (code) => console.log('worker exited', code))
w.kill()
```

The entry may be absolute, relative to `app.workersDir()`, or start with `./`.
`ow dev`/`ow build` compile `app/workers/**` and point `OW_APP_WORKERS` at them.
Because the worker runs on the same real Node, it can `require` native addons
(see [Native addons](native-addons.md)).

## Tips

- Keep the main process thin. Window orchestration, menus, tray, and Node-only
  integrations belong here; everything else can go in the renderer.
- `app.whenReady()` is idempotent and returns the same promise, so calling it
  from several modules is fine.
- The control socket can drop (kernel crash). Listen for `disconnected` on
  windows to surface it.

## Next steps

- [`app` reference](../api/sdk/app.md).
- [`BrowserWindow`](windows.md) and [`WebContents`](../api/sdk/web-contents.md).
- [Workers reference](../api/sdk/workers.md).
