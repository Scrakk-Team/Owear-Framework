---
title: Renderer API (window.ow)
description: The kernel injects window.ow into every document it loads. This object is your
order: 18
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Renderer API (`window.ow`)

The kernel injects `window.ow` into every document it loads. This object is your
direct line to the native kernel — there is no preload script and no IPC to a
Node process by default.

```ts
// Call any native module function
const text = await ow.invoke<string>('fs', 'readText', '/etc/hostname')
```

## Surface

```ts
ow.invoke<T>(module: string, fn: string, ...args: unknown[]): Promise<T>
  // rejects with Error('ow: timeout <module>/<fn>') if the kernel does not
  // answer within 30 s

ow.invokeSync(module: string, fn: string, ...args: unknown[]): unknown
  // ⚠️ blocks the renderer. Bootstrap only. Uses ow-sync:// internally.

ow.readShared(handle: { id: string; size: number }): Promise<ArrayBuffer>
  // reads a shared-memory region published by the kernel (payloads ≥ 256 KB)
  // without going through JSON/base64

ow.on(name: string, cb: (payload: unknown) => void): () => void
  // subscribe to events; returns an unsubscribe function

ow.emit(name: string, payload?: unknown): void
  // JS → native + other JS listeners

ow.emitTo(targetWindowId: number, name: string, payload?: unknown): void
  // directed window → window message

ow.findInPage(text: string, opts?: { matchCase?: boolean; backwards?: boolean })
  : { matches: number; active: number }
  // JS helper around window.find

window.__owWindowId: number
  // the id of the current window
```

`src/ow.d.ts` in a scaffolded app declares this surface for TypeScript.

## Calling modules

`ow.invoke(module, fn, ...args)` marshals `args` as a JSON array and calls the
matching native function. The kernel resolves the module (a `.owm` loaded with
`dlopen`, or a builtin linked into the kernel) and runs the function.

```ts
// builtin modules (linked into the kernel)
await ow.invoke('dialog', 'showOpenDialog', { properties: ['openFile'] })
await ow.invoke('session', 'cookiesGet', 'https://example.com')

// stock .owm modules
await ow.invoke('fs', 'writeFile', '/tmp/a.txt', 'hello')
await ow.invoke('screen', 'getAllDisplays')

// your own modules (native/*.cpp)
await ow.invoke('files', 'readText', '/etc/hostname')
```

Errors from native code reject the promise with the returned message.

### Synchronous calls

`ow.invokeSync` uses a blocking XHR to `ow-sync://` and is only safe before the
app has interactive state. It exists for bootstrap tasks (for example reading a
synchronous config before the first paint). Do not call it from event handlers.

```ts
const theme = ow.invokeSync('theme', 'isDark') as boolean
```

## Large binary payloads

When a function returns a payload of 256 KB or more, the kernel does not embed it
in JSON. Instead it publishes the bytes to a shared-memory region and returns a
handle:

```ts
type ShmHandle = { __ow_shm: { id: string; size: number } }
```

Read it with `ow.readShared`, which returns an `ArrayBuffer` served from the
mmap — no base64, no extra kernel copy:

```ts
const res = await ow.invoke<{ __ow_shm?: ShmHandle['__ow_shm']; b64?: string }>(
  'fs', 'readFile', '/tmp/big.iso',
)
if (res.__ow_shm) {
  const buf = await ow.readShared(res.__ow_shm)   // ArrayBuffer
}
```

Functions documented as returning `__ow_shm` include `fs.readFile`, `fs.read`,
`net.request` (large bodies), `clipboard.readImage`, `capturer.captureScreen`,
and `window.capturePage`.

## Events

Events flow from the kernel to the renderer:

```ts
const off = ow.on('fs.watch', (payload) => {
  // payload: { watcherId, events: [{ type, path }] }
})
// later
off()
```

`ow.emit(name, payload)` goes the other way (JS → native) and also notifies
other JS listeners in the same document. `ow.emitTo(targetWindowId, name, payload)`
sends an event to a specific other window.

## DOM attributes the kernel understands

The bridge scans documents for a few attributes related to custom title bars:

```html
<div data-ow-drag>…</div>                         <!-- window drag region -->
<button data-ow-no-drag>…</button>                <!-- excluded from drag -->
<div data-ow-resize="bottom-right">…</div>        <!-- manual resize handle -->
```

Resize edges: `left | right | top | bottom | top-left | top-right | bottom-left |
bottom-right`.

## When to use the main process instead

`ow.invoke` is the right tool for almost everything: it is the shortest path to
native code. Reach for `ow.invoke('node', 'call', { fn, args })` only when you
need actual Node — a database driver, a native addon, or an existing Node
library. See [IPC](ipc.md) and [Main process](main-process.md).

## Next steps

- [IPC](ipc.md) — talking to the main process.
- [Native modules](native-modules.md) — adding your own functions.
- [Renderer reference](../api/renderer.md) — the same content, fully formalized.
