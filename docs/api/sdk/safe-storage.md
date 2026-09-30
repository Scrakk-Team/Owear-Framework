---
title: safeStorage
description: Encrypt small secrets using the OS. Backed by the native safestorage module.
order: 15
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `safeStorage`

Encrypt small secrets using the OS. Backed by the native `safestorage` module.

```ts
import { safeStorage } from '@owear/core'
```

## API

```ts
interface SafeStorageResult {
  data: string       // encrypted string (or the plain text if no backend)
  encrypted: boolean // true if a real backend was used
}

safeStorage.isAvailable(): Promise<boolean>
safeStorage.encrypt(text: string): Promise<SafeStorageResult>
safeStorage.decrypt(data: string): Promise<string>
```

`decrypt` accepts both encrypted blobs and plain text, returning the original
string either way (values stored before a backend existed remain readable).

## Example

```ts
if (await safeStorage.isAvailable()) {
  const { data } = await safeStorage.encrypt(token)
  store(data)
  // later
  const token = await safeStorage.decrypt(store.read())
}
```

## Backends

- Windows: DPAPI.
- Linux: AES-256-GCM with a 32-byte random key at
  `$XDG_DATA_HOME/owear/<OW_APP_ID>/safe-key.bin`, permissions `0600`. The blob
  format is `ow1:` + base64 of `iv[12] | tag[16] | ciphertext`.

Secrets are not portable across machines or users; that is intentional. See the
[Safe storage guide](../../guides/safe-storage.md).
