---
title: protocol
description: Register custom schemes served either from a directory or from a main-process
order: 14
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `protocol`

Register custom schemes served either from a directory or from a main-process
handler. Related export: `app.protocol` (the recommended entry point) and the
`protocolHandlers` map.

```ts
import { app } from '@owear/core'
```

## `app.protocol`

```ts
app.protocol(name: string, options?: {
  privileged?: { secure?: boolean; cors?: boolean; stream?: boolean; standard?: boolean; fetch?: boolean }
  serve?: string
  handler?: ProtocolHandler
}): Promise<void>
```

- `serve` — the kernel serves files from that directory for `name://…`.
- `handler` — the main process answers each request (takes precedence).
- `privileged` — flags that give the scheme special powers in the WebView.

## Handler

```ts
interface ProtocolRequest {
  url: string
  method: string
  headers: Record<string, string>
  body: string   // base64
}

type ProtocolHandlerResult =
  | Response
  | { status?: number; headers?: Record<string, string>; body?: string | Uint8Array }
  | string
  | null
  | undefined

type ProtocolHandler = (req: ProtocolRequest) => ProtocolHandlerResult | Promise<ProtocolHandlerResult>
```

Return values are normalized:

| Return | Result |
|---|---|
| `null` / `undefined` | 404 |
| `string` | 200 with `content-type: text/html` |
| `Response` | its status, headers, and body |
| `{ status?, headers?, body? }` | as given (defaults: 200, `{}`, empty) |

A string `body` and a `Uint8Array` body are both accepted; strings are UTF-8
encoded, bytes are passed through.

## Example

```ts
app.protocol('myapp', {
  privileged: { secure: true, cors: true, standard: true },
  handler: async (req) => {
    const path = new URL(req.url).pathname
    if (path === '/time') return JSON.stringify({ now: Date.now() })
    return new Response('<h1>Not found</h1>', { status: 404, headers: { 'content-type': 'text/html' } })
  },
})

// serve static assets from disk
app.protocol('assets', { serve: '/opt/myapp/resources' })
```

Pages can then load `myapp://…` and `assets://…`.

## Notes

- `serve` paths are validated by the kernel to prevent traversal outside the
  served directory.
- Registering a scheme after windows already exist also applies to them
  (`Window::RegisterProtocol`).
- The handler runs in Node, so it can read files, hit a database, etc. Only mark
  a scheme `secure` if you fully control what it serves.
