---
title: net
description: Native HTTP(S) requests without CORS, plus file downloads with SHA-256.
order: 11
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `net`

Native HTTP(S) requests without CORS, plus file downloads with SHA-256.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

## Functions

### `request`

```ts
net.request({
  method?: 'GET' | 'POST' | 'PUT' | 'DELETE',
  url: string,
  headers?: { [k: string]: string },
  body?: string,
  bodyB64?: string,
  timeoutMs?: number,
}) → { status: number, headers: { [k: string]: string }, body: string | { id: string, size: number } }
```

- `method` defaults to `GET`.
- `timeoutMs` is rounded to whole seconds (minimum 1), default 30 s.
- Response header names are lowercased.
- If the response body is **≥ 256 KB**, `body` is a shared-memory handle
  `{ id, size }`; read it with `ow.readShared`.

```ts
const res = await ow.invoke<any>('net', 'request', {
  method: 'POST',
  url: 'https://api.example.com/items',
  headers: { 'content-type': 'application/json' },
  body: JSON.stringify({ name: 'note' }),
})

const text = typeof res.body === 'string'
  ? res.body
  : Buffer.from(await ow.readShared(res.body)).toString('utf8')
```

### `download`

```ts
net.download(url, destPath) → { sha256: string }
```

Streams the file to `destPath` and returns the SHA-256 hex digest of the bytes.

```ts
const { sha256 } = await ow.invoke<{ sha256: string }>(
  'net', 'download', 'https://example.com/big.iso', '/home/me/big.iso',
)
```

## Notes

- Requests originate in the kernel, so they are not subject to page CORS and may
  carry arbitrary headers.
- The transport is the kernel's own TLS client (`Runtime/Http`), with its own
  limits; `timeoutMs` is second-granularity.
- The same module backs the auto-updater's HTTP needs.

## See also

- [Networking guide](../../guides/networking.md)
- [Auto-update guide](../../guides/auto-update.md)
