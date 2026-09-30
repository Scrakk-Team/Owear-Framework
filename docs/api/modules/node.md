---
title: node
description: Bridge from the renderer to the main (Node) process.
order: 12
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `node`

Bridge from the renderer to the main (Node) process.

- **Kind:** builtin
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

This is the only module whose calls are forwarded out of the kernel. Everything
else runs `renderer → kernel → module` directly; `node` routes through the
control socket to the main process.

## Functions

| Function | Signature |
|---|---|
| `call` | `({ fn, args? }) → result` |

## Usage

Register a handler in the main process:

```ts
import { app } from '@owear/core'
app.handle('db.query', async (sql: string) => db.prepare(sql).all())
```

Call it from the renderer:

```ts
const rows = await ow.invoke('node', 'call', { fn: 'db.query', args: ['select 1'] })
```

The kernel forwards the call as `node.request` to the main process, which
dispatches to the handler and answers with `node.respond`. The handler's return
value (or an error message) comes back to the renderer.

Main → renderer events use `app.send`, received with `ow.on`:

```ts
// main
app.send('job.progress', { percent: 42 })
// renderer
ow.on('job.progress', ({ percent }) => update(percent))
```

## Notes

- Opt-in: only use it for features that genuinely need Node. For filesystem,
  dialogs, clipboard, and the like, call the native module directly.
- Use `app.handleContext` when the handler needs the calling window id.
- Reserved handler `__ow_port_post` is used internally by [message ports](../../api/sdk/ports.md).

## See also

- [IPC guide](../../guides/ipc.md)
- [Node IPC SDK](../../api/sdk/node-ipc.md)
