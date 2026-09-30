---
title: Architecture overview
description: Owear is built from three cooperating pieces, plus an on-demand module system.
order: 4
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Architecture overview

Owear is built from three cooperating pieces, plus an on-demand module system.

```text
┌─────────────────────────────────────────────┐
│  App process (native, ~5-10 MB)             │
│  kernel: app · windows · bridge · loader    │
│  .owm modules: fs · dialog · yours          │
└──────────────┬──────────────────────────────┘
               │ direct bridge (no Node in between)
     OS WebView  ←  your frontend (Vite/React/…)
               │
     Node sidecar (auto-installed, optional)
       └── app/main.ts — Electron-like API
```

## The kernel

A native binary (`owear`) written in C++20. It owns:

- the **app loop** (GTK / Win32) and window management;
- the **WebView** backend for the OS (WebKitGTK / WebView2);
- the **bridge** injected into every document;
- the **module loader** that `dlopen`s `.owm` files;
- the **control server** that the Node SDK talks to.

See [Kernel and lifecycle](kernel.md).

## The renderer

Your web frontend. The kernel injects `window.ow` into every document, so the UI
calls native modules directly:

```ts
const text = await ow.invoke('fs', 'readText', '/etc/hostname')
```

There is no preload script and no mandatory IPC hop through Node. See
[Bridge](bridge.md) and the [Renderer guide](../guides/renderer-api.md).

## The main process (optional)

A Node process (the "sidecar") started by the kernel when `OW_APP_MAIN` is set.
It uses `@owear/core`, an Electron-shaped SDK, and talks to the kernel over the
control socket. Use it for Node-only work and for window/menu/tray orchestration.
See the [Main process guide](../guides/main-process.md).

## Modules

Native capabilities are packaged as modules:

- **`.owm` modules** are standalone shared libraries loaded on demand and
  callable from the renderer or main process. The stock modules (`fs`, `path`,
  `process`, `net`, `screen`, …) are `.owm`s.
- **Builtins** are coupled to the kernel (they need the WebView or the control
  server) and are linked in: `window`, `session`, `webview`, `node`,
  `installer`, `crashreporter`, `app`.

A manifest (`api/<name>/owear.module.json`) drives discovery and registration.
See [Module manifests](../reference/manifests.md).

## Data paths

| Path | Transport |
|---|---|
| Renderer → kernel | injected bridge (`ow.invoke`), JSON + shared memory for large payloads |
| Kernel → renderer | batched `eval` of `_apply`/`_event` payloads |
| SDK ↔ kernel | NDJSON over a Unix domain socket / named pipe |
| Renderer → main (Node) | `node/call` forwarded through the kernel to the sidecar |

## Design principles

- **Anti-drift:** public contracts live in `include/ow/` and one implementation
  per platform is chosen by CMake, never `#ifdef`. Breaking a signature breaks
  compilation on all platforms by design.
- **Small core:** the kernel is ~5–10 MB and loads only the modules you use.
- **No bundled engine:** use the OS WebView and a runtime-resolved Node.
- **Explicit memory contract:** module responses are valid only during the call.
