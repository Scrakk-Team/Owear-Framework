---
title: nativeImage and NativeImage
description: A small image class with no native dependencies  a full PNG codec (using
order: 9
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `nativeImage` and `NativeImage`

A small image class with **no native dependencies**: a full PNG codec (using
Node's zlib), bilinear resize, and crop. Identical on Linux and Windows.

```ts
import { nativeImage, NativeImage } from '@owear/core'
```

## Factory

```ts
nativeImage.createFromPath(path: string): NativeImage
nativeImage.createFromBuffer(buf: Buffer): NativeImage
nativeImage.createFromDataURL(dataURL: string): NativeImage
```

All three are synchronous, like Electron.

## `NativeImage`

```ts
img.isEmpty(): boolean
img.getSize(): { width: number; height: number }
img.toPNG(): Buffer
img.toJPEG(quality?: number): Buffer
img.toDataURL(): string
img.resize(options: { width?; height?; quality?: 'good' | 'better' | 'best' }): NativeImage
img.crop(rect: { x; y; width; height }): NativeImage
```

`resize` preserves aspect ratio if only one of `width`/`height` is given.

## Format support

| Format | Range |
|---|---|
| PNG | color types 0/2/3/4/6, bit depths 1/2/4/8/16, non-interlaced; decode to RGBA and encode from RGBA |
| JPEG | `getSize` (SOF header) and `toJPEG` (returns the original bytes when the source is already JPEG); resizing JPEG is not supported |

Methods that require decoded pixels (`toPNG` when the source is not PNG,
`resize`, `crop`) throw if the image was not decodable.

## Example

```ts
const shot = await win.webContents.capturePage()   // NativeImage
const thumb = shot.resize({ width: 128 })
const icon = thumb.crop({ x: 0, y: 0, width: 16, height: 16 })
fs.writeFileSync('thumb.png', thumb.toPNG())
tray.setImage(icon)
```

## Types

```ts
interface Size { width: number; height: number }
interface Rectangle { x: number; y: number; width: number; height: number }
interface ResizeOptions { width?; height?; quality? }
```
