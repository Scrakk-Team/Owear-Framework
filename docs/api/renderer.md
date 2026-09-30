---
title: Renderer API — window.ow
description: The kernel injects window.ow into every document it loads. All calls from the
order: 1
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Renderer API — `window.ow`

The kernel injects `window.ow` into every document it loads. All calls from the
renderer go through this object. It is a distinct surface from `@owear/core`,
which is used in the main process.

> Types for this object live in a scaffolded app's `src/ow.d.ts`, maintained by
> hand against this page.

## Methods

### `ow.invoke`

```ts
ow.invoke<T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T>
```

Calls a function of a native module. `args` are passed as a JSON array. Rejects
with the native error message on failure. Rejects with
`Error('ow: timeout <module>/<fn>')` if the kernel does not respond within 30
seconds.

```ts
const text = await ow.invoke<string>('fs', 'readText', '/etc/hostname')
const { filePaths } = await ow.invoke('dialog', 'showOpenDialog', { properties: ['openFile'] })
```

### `ow.invokeSync`

```ts
ow.invokeSync(module: string, fn: string, ...args: unknown[]): unknown
```

Synchronous variant, implemented with a blocking XHR to `ow-sync://`.

> ⚠️ **Blocks the renderer** and risks reentrancy. Use only during bootstrap,
> before interactive state exists. Never call from event handlers.

```ts
const dark = ow.invokeSync('theme', 'isDark') as boolean
```

### `ow.readShared`

```ts
ow.readShared(handle: { id: string; size: number }): Promise<ArrayBuffer>
```

Reads a shared-memory region published by the kernel and returns an
`ArrayBuffer` served from the mmap. This is how payloads ≥ 256 KB avoid
JSON/base64. The region is served through the `ow-shm://` scheme.

```ts
const res = await ow.invoke<any>('fs', 'readFile', '/tmp/big.bin')
const bytes = res.__ow_shm ? await ow.readShared(res.__ow_shm) : decode(res.b64)
```

### `ow.on`

```ts
ow.on(name: string, cb: (payload: unknown) => void): () => void
```

Subscribes to an event. Returns an unsubscribe function.

```ts
const off = ow.on('fs.watch', ({ watcherId, events }) => {})
off()
```

### `ow.emit`

```ts
ow.emit(name: string, payload?: unknown): void
```

Sends an event from JS to the native layer and to other JS listeners in the same
document.

### `ow.emitTo`

```ts
ow.emitTo(targetWindowId: number, name: string, payload?: unknown): void
```

Directed window → window event.

### `ow.findInPage`

```ts
ow.findInPage(text: string, opts?: { matchCase?: boolean; backwards?: boolean })
  : { matches: number; active: number }
```

A JS helper around `window.find`, returning match counts. For the native find
controller (with options) use the `window` module in a renderer with the kernel,
or `webContents` operations.

## Globals

| Global | Type | Meaning |
|---|---|---|
| `window.__owWindowId` | `number` | Id of the current window |
| `window.__owTitlebarOverlay` | `{ enabled, height, width, top?, right? } \| undefined` | Present when native title-bar buttons are overlaid |

## Schemes available from the WebView

| Scheme | Use |
|---|---|
| `app://<path>` | Assets from the app bundle (`dist/`) |
| `ow-shm://<id>` | Shared-memory region (no copy) |
| `ow-sync://i/<payload>` | Internal to `invokeSync` |

## DOM attributes

```html
<div data-ow-drag>…</div>                       <!-- drag region -->
<button data-ow-no-drag>…</button>              <!-- excluded from drag -->
<div data-ow-resize="bottom-right">…</div>      <!-- manual resize handle -->
```

Resize edges: `left | right | top | bottom | top-left | top-right | bottom-left |
bottom-right`.

## Related

- [Bridge and wire format](../architecture/bridge.md).
- [Renderers guide](../guides/renderer-api.md).
