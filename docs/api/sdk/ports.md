---
title: Message ports
description: A bidirectional channel between the main process and a renderer, modeled after
order: 12
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Message ports

A bidirectional channel between the main process and a renderer, modeled after
`MessagePort`. Messages travel as JSON over the bridge. For binary payloads, use
shared memory (`ow.readShared`) instead.

## Create and transfer

```ts
import { app } from '@owear/core'

const { port1, port2 } = app.createChannel()   // both ends start in the main process
await app.sendPort(win.id!, 'stream', port2)   // transfer one end to a window
```

Receive it in the renderer:

```ts
ow.on('stream', ({ port }) => {
  const p = (ow as any).port(port)
  p.on('message', (m) => console.log('from main:', m))
  p.postMessage('hello from renderer')
})
```

## Main-side API

```ts
interface MessagePortMain {
  readonly portId: number
  postMessage(message: unknown): void
  on(event: 'message', listener: (message: unknown) => void): this
  start(): void
  close(): void
}
```

```ts
port1.on('message', (m) => port1.postMessage({ echo: m }))
port1.postMessage({ hello: true })
port1.close()
```

## How it works

Each endpoint is registered by id. `postMessage` routes the message to the peer:
if the peer lives in the main process, it emits `message` directly; if it lives in
a renderer, the SDK calls `node.emit` with the reserved event `__ow_port:msg` and
the target `windowId`. The renderer posts back through the reserved handler
`__ow_port_post`.

## Notes

- Both ports are created in the main process; transfer the one you want the
  renderer to own with `app.sendPort`.
- Ports are JSON-only. Do not send `ArrayBuffer`s here.
- `MessagePortMain` extends an EventEmitter, so standard `on`/`once`/`off` apply.
