---
title: Networking
description: The net module performs HTTP(S) requests from native code, bypassing the
order: 15
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Networking

The `net` module performs HTTP(S) requests from native code, bypassing the
browser's CORS rules, and downloads files with SHA-256 verification. It uses the
kernel's own TLS client.

## Requests

```ts
const res = await ow.invoke<{
  status: number
  headers: Record<string, string>   // lowercase keys
  body: string | { id: string; size: number }
}>('net', 'request', {
  method: 'POST',
  url: 'https://api.example.com/items',
  headers: { 'content-type': 'application/json' },
  body: '{"name":"note"}',
  // or bodyB64 for binary payloads
  timeoutMs: 15000,
})
```

```ts
net.request({
  method: 'GET' | 'POST' | 'PUT' | 'DELETE',
  url,
  headers?: { [k: string]: string },
  body?: string,
  bodyB64?: string,
  timeoutMs?: number,
}): Promise<{ status, headers, body }>
```

- `method` defaults to `GET`.
- `timeoutMs` is rounded to whole seconds (minimum 1 s), default 30 s.
- Response headers use lowercase names.
- Responses **≥ 256 KB** return `body` as a shared-memory handle:

```ts
if (typeof res.body !== 'string') {
  const bytes = await ow.readShared(res.body)   // ArrayBuffer
}
```

Because requests originate in the kernel, they are not subject to page CORS and
can carry arbitrary headers. Treat this as a capability: only expose it to
trusted UI.

## Downloads

```ts
const { sha256 } = await ow.invoke<{ sha256: string }>(
  'net', 'download', 'https://example.com/big.iso', '/home/me/big.iso',
)
```

```ts
net.download(url, destPath): Promise<{ sha256: string }>
```

The file is streamed to `destPath`; the returned `sha256` is a hex digest of the
downloaded bytes for integrity checks.

## When to use `net` vs the page's `fetch`

| Situation | Use |
|---|---|
| Same-origin API from your UI | `fetch` in the renderer |
| Third-party API without CORS | `net.request` |
| Large download that must not block the UI | `net.download` |
| Requests from the main process | `net.request` via `invokeNative`, or Node `fetch` |

## Next steps

- [`net` module reference](../api/modules/net.md).
- [Auto-update](auto-update.md) — built on the same HTTP client.
