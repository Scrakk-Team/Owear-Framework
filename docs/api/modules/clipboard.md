---
title: clipboard
description: System clipboard  text and images (PNG), via the OS clipboard.
order: 4
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `clipboard`

System clipboard: text and images (PNG), via the OS clipboard.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

## Functions

| Function | Signature |
|---|---|
| `readText` | `() → string` |
| `writeText` | `(text) → null` |
| `readImage` | `() → { __ow_shm, width, height, format } \| null` |
| `writeImage` | `(pngBase64) → null` |
| `clear` | `() → null` |

## Reading an image

`readImage` returns `null` when there is no image, or a shared-memory handle:

```ts
const img = await ow.invoke<any>('clipboard', 'readImage')
if (img) {
  const png = await ow.readShared(img.__ow_shm)   // ArrayBuffer
  console.log(img.width, img.height, img.format)  // 'png'
}
```

## Writing an image

```ts
const pngBase64 = Buffer.from(await ow.readShared(handle)).toString('base64')
await ow.invoke('clipboard', 'writeImage', pngBase64)
```

## Notes

- Images always travel as PNG.
- Linux uses `GtkClipboard`; Windows uses WIC (pending). Text works on both
  platforms.
- `clear()` empties the system clipboard selection.

## See also

- [Clipboard and shell guide](../../guides/clipboard-and-shell.md)
