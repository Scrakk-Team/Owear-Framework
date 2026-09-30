---
title: IPC (renderer ↔ main process)
description: Owear is designed so the renderer talks to the kernel directly. The main process
order: 9
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# IPC (renderer ↔ main process)

Owear is designed so the renderer talks to the kernel directly. The main process
is **opt-in**: only features that genuinely need Node take the extra hop. This
page covers the three mechanisms you will use.

| Direction | Mechanism | Use when |
|---|---|---|
| Renderer → native kernel | `ow.invoke(module, fn, …)` | Always, for anything the kernel or a `.owm` offers |
| Renderer → main (Node) | `ow.invoke('node', 'call', { fn, args })` | You need Node or a Node library |
| Main → renderer | `app.send(name, payload, windowId?)` | Push an event to the UI |
| Renderer → renderer | `ow.emitTo(targetWindowId, name, payload)` | Message another window |
| Main ↔ main renderer (stream) | `MessagePort` via `app.createChannel()` | High-frequency bidirectional messaging |

## Renderer → main: `node/call`

Register a handler in the main process:

```ts
// app/main.ts
import { app } from '@owear/core'

app.handle('db.query', async (sql: string, params: unknown[]) => {
  return db.prepare(sql).all(params)     // e.g. better-sqlite3
})

// or, when the handler needs to know which window called it:
app.handleContext('window.closeSelf', (ctx, reason: string) => {
  console.log('called from window', ctx.windowId, reason)
  return null
})
```

Call it from the renderer:

```ts
const rows = await ow.invoke('db', 'query', ['select * from notes'])
//        ↳ module 'node' is what the kernel forwards to the main process
```

Precisely, the renderer sends `ow.invoke('node', 'call', { fn, args })`. The
kernel forwards it over the control socket as `node.request`; the main process
dispatches to the handler registered with `app.handle` and answers with
`node.respond`:

```ts
// equivalent, explicit form
const rows = await ow.invoke('node', 'call', { fn: 'db.query', args: [sql, params] })
```

Handlers may be async; the resolved value (or a thrown error's message) is
returned to the renderer. Only the `node` module is forwarded this way — every
other module goes straight to the kernel.

### When to use it

| Need | Solution |
|---|---|
| Read a file | `ow.invoke('fs', …)` — do **not** use Node |
| Talk to SQLite/`raw`/`sharp` | `app.handle` + `ow.invoke('node', 'call', …)` |
| Parse with a Node-only library | `app.handle` |
| Run heavy work off the UI thread | `app.forkWorker` |

## Main → renderer: `app.send`

```ts
// main
app.send('update.available', { version: '1.4.0' })        // all windows
app.send('toast', 'Saved', win.id)                        // one window
```

```ts
// renderer
ow.on('update.available', ({ version }) => showBanner(version))
ow.on('toast', (msg) => console.log(msg))
```

`webContents.send(name, payload)` does the same but is bound to a single
window's contents.

## Renderer → renderer: `ow.emitTo`

```ts
// in window A
ow.emitTo(2, 'editor.save', { path: '/notes/a.md' })

// in window 2
ow.on('editor.save', ({ path }) => save(path))
```

You can also leave `windowId` out of `app.send` to broadcast.

## Streaming: `MessagePort`

For high-frequency or long-lived conversations, create a channel in the main
process and transfer one end to a window:

```ts
// main
const win = new BrowserWindow({ ... })
win.on('ready-to-show', async () => {
  const { port1, port2 } = app.createChannel()
  port1.on('message', (m) => port1.postMessage({ echo: m }))
  await app.sendPort(win.id!, 'stream', port2)
})
```

```ts
// renderer
ow.on('stream', ({ port }) => {
  const p = (ow as any).port(port)      // wraps the transferred endpoint
  p.on('message', (m) => console.log('from main:', m))
  p.postMessage('hello')
})
```

Messages are JSON, like the rest of the bridge; for binary payloads use shared
memory (`ow.readShared`) instead.

## What not to do

- Do not route filesystem, dialogs, or clipboard through Node. They are native
  modules and going direct is both simpler and faster.
- Do not use `ow.invokeSync` in handlers — it blocks the renderer.
- Do not assume the main process exists. A valid app can have no `app/main.ts`.

## Next steps

- [Main process](main-process.md) — `app`, workers, lifecycle.
- [`node` IPC reference](../api/sdk/node-ipc.md) and [ports](../api/sdk/ports.md).
