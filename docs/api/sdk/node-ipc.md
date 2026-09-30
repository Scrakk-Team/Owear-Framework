---
title: Node IPC (app.handle / app.send)
description: The bridge between the renderer and the main process for features that need Node.
order: 11
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Node IPC (`app.handle` / `app.send`)

The bridge between the renderer and the main process for features that need Node.
It is opt-in: only calls made through the `node` module take this path.

## Registering handlers

```ts
import { app } from '@owear/core'

type NodeHandler = (...args: any[]) => unknown | Promise<unknown>
type ContextNodeHandler = (ctx: { windowId: number }, ...args: any[]) => unknown | Promise<unknown>

app.handle(fn: string, handler: NodeHandler): () => void
app.handleContext(fn: string, handler: ContextNodeHandler): () => void
```

Both return an unsubscribe function. `handleContext` receives the originating
window id (`0` when the call did not come from a specific window).

```ts
app.handle('db.query', async (sql: string) => db.prepare(sql).all())
app.handleContext('window.closeSelf', (ctx, reason: string) => {
  const win = BrowserWindow.fromId(ctx.windowId)
  win?.close()
  return null
})
```

## Calling from the renderer

```ts
// generic
const rows = await ow.invoke('node', 'call', { fn: 'db.query', args: ['select 1'] })

// typed helper
const r = await ow.invoke('node', 'call', { fn: 'extension.activate', args: [id] })
```

Resolution is **asynchronous**: the kernel forwards `node/call` to the main
process over the control socket (`node.request`) and the main answers with
`node.respond`. If the handler throws, the renderer's promise rejects with the
error message.

## Main → renderer

```ts
app.send(name: string, payload?: unknown, windowId?: number): Promise<void>
```

Leaves `windowId` out to broadcast to all windows; otherwise sends to one.

```ts
app.send('extension.event', { kind: 'diagnostics' })
app.send('toast', 'Saved', win.id)
```

The renderer receives it with `ow.on(name, cb)`.

## Notes

- Handlers run in the main process; keep them fast or move heavy work to a
  [worker](workers.md).
- The `node` module has exactly one function, `call`. Everything else goes
  straight to the kernel.
- Reserved handler: `__ow_port_post` is used internally by [ports](ports.md).
