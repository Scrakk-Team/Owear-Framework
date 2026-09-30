---
title: Safe storage
description: safeStorage encrypts small secrets (tokens, passwords, keys) using the
order: 19
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Safe storage

`safeStorage` encrypts small secrets (tokens, passwords, keys) using the
operating system's facilities, so you never store them in plain text on disk.

```ts
import { safeStorage } from '@owear/core'

if (await safeStorage.isAvailable()) {
  const { data, encrypted } = await safeStorage.encrypt('my-secret-token')
  // store `data` anywhere; it is a string
  const plain = await safeStorage.decrypt(data)
}
```

## API

```ts
safeStorage.isAvailable(): Promise<boolean>
safeStorage.encrypt(text): Promise<{ data: string; encrypted: boolean }>
safeStorage.decrypt(data): Promise<string>
```

- `encrypt` returns `{ data, encrypted }`. `encrypted` is `true` when a real
  backend was used.
- `decrypt` accepts **both** encrypted blobs and plain text (values stored when
  no backend was available), returning the original string either way.

## Backends

| Platform | Mechanism |
|---|---|
| Windows | DPAPI |
| Linux | AES-256-GCM with a 32-byte random key stored in the app data dir with `0600` permissions |

On Linux the encrypted blob format is `ow1:` + base64 of `iv[12] | tag[16] |
ciphertext`. The key lives at (roughly)
`$XDG_DATA_HOME/owear/<OW_APP_ID>/safe-key.bin`. Because the key is local to the
machine and user, encrypted values are not portable across machines — that is the
intended trade-off.

## Limitations

- It protects secrets **at rest on the same machine**, not against a process
  running as the same user.
- Do not use it for large data; it is meant for credentials.
- If the key file is lost or the user changes, `decrypt` fails with a clear
  error rather than returning garbage.

## Next steps

- [`safestorage` module reference](../api/modules/safestorage.md).
- [Security](security.md) — the broader hardening checklist.
