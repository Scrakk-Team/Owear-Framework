---
title: safestorage
description: OS-backed encryption for small secrets.
order: 17
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `safestorage`

OS-backed encryption for small secrets.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

## Functions

| Function | Signature |
|---|---|
| `isAvailable` | `() → boolean` |
| `encrypt` | `(text) → { data, encrypted }` |
| `decrypt` | `(data) → string` |

## Behavior

- `encrypt` returns `{ data, encrypted }`; `encrypted` is `true` when a real
  backend was used.
- `decrypt` accepts both encrypted blobs and plain text, so values stored before
  a backend existed remain readable.

## Backends

- Windows: DPAPI.
- Linux: AES-256-GCM with a 32-byte random key at
  `$XDG_DATA_HOME/owear/<OW_APP_ID>/safe-key.bin` (`0600`). The blob format is
  `ow1:` + base64 of `iv[12] | tag[16] | ciphertext`.

Secrets are local to the machine/user and not portable; `decrypt` fails with a
clear error if the key or blob is invalid.

## See also

- [Safe storage guide](../../guides/safe-storage.md)
- [`safeStorage` SDK](../../api/sdk/safe-storage.md)
