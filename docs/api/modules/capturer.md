---
title: capturer
description: Screen capture and available sources.
order: 3
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `capturer`

Screen capture and available sources.

- **Kind:** module (`.owm`), **optional**
- **Version:** 0.1.0
- **Platforms:** Linux (X11), Windows

> **v1 limitation:** on Linux capture works on **X11 only**. Under Wayland the
> functions return a clear `capture is only supported on an X11 session` error
> (an xdg-desktop-portal backend is planned).

## Functions

| Function | Signature |
|---|---|
| `getSources` | `() → Source[]` |
| `captureScreen` | `(screenIndex?) → { __ow_shm, width, height, format }` |

## `getSources`

Returns one entry per monitor:

```ts
{
  type: 'screen',
  id: number,
  name: string,
  bounds: { x, y, width, height },
  thumbnail: { id: string, size: number }   // PNG in shared memory
}[]
```

Each `thumbnail` is a shared-memory handle; read it with `ow.readShared`.

## `captureScreen`

Captures a full-resolution PNG of a monitor (default index `0`):

```ts
const shot = await ow.invoke<any>('capturer', 'captureScreen', 0)
const png = await ow.readShared(shot.__ow_shm)   // ArrayBuffer
console.log(shot.width, shot.height, shot.format) // 'png'
```

## Implementation notes

- X11: `XGetImage` on the root window, converted to a GdkPixbuf and encoded as
  PNG.
- The module is marked optional.
- Windows (`BitBlt`) is the intended backend; verify in CI.

## See also

- [Screen and power guide](../../guides/screen-and-power.md)
- [`screen`](screen.md)
