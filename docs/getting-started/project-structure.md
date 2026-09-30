---
title: Project structure
description: A scaffolded app (ow create my-app) looks like this 
order: 3
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Project structure

A scaffolded app (`ow create my-app`) looks like this:

```text
my-app/
├── app/
│   ├── main.ts            # Node main process (sidecar)
│   └── workers/           # optional Node workers (compiled to dist/workers)
├── native/                # optional: your C++ modules (*.cpp → *.owm)
├── public/                # static assets copied verbatim by Vite
├── src/
│   ├── renderer.ts        # UI entry point (runs in the WebView)
│   ├── style.css
│   ├── ow.d.ts            # types for window.ow (injected by the kernel)
│   └── owear-native.d.ts  # generated types for @owear/native
├── index.html             # the document the WebView loads
├── vite.config.ts
├── package.json
└── tsconfig.json
```

## The renderer (`src/`, `index.html`)

This is a normal Vite app. It is loaded inside the OS WebView. The kernel injects
`window.ow` into every document, which is how the UI reaches native code directly
— there is no `ipcRenderer` and no round trip through Node.

```ts
// src/renderer.ts
const files = await window.ow.invoke<string[]>('fs', 'readDir', '/tmp')
```

`src/ow.d.ts` declares the `window.ow` surface so TypeScript knows about it.
Note that it is a *type* file — the object itself is injected at runtime.

## The main process (`app/main.ts`)

Optional but present in the template. It runs on a real Node process ("sidecar")
and uses `@owear/core`, whose API mirrors Electron:

```ts
import { app, BrowserWindow, Menu, Tray } from '@owear/core'
```

Use it for:

- creating and orchestrating windows;
- native menus and tray icons (they need the main process in the SDK);
- Node-only code (database drivers, `child_process`, native addons);
- exposing privileged operations to the renderer via `app.handle`.

If `app/main.ts` does not exist, the kernel opens a window at `OW_START_URL`
directly (see [Dev and build](dev-and-build.md)).

## Native modules (`native/`)

Drop `.cpp` files here to add native functions to your app. Each file becomes a
`.owm` module, callable from the renderer.

```cpp
// native/files.cpp
#include <ow/Module.h>

static void readText(const ow_request_t* req, ow_response_t* res) {
    ow::Module::RespondOk(res, "\"hello from C++\"");
}

OW_MODULE_BEGIN(files, "1.0.0")
OW_FN(readText)
OW_MODULE_END()
```

```ts
import { files } from '@owear/native'
const text = await files.readText()
```

See [Native modules](../guides/native-modules.md). The Vite plugin compiles these
for you in dev and build, and generates `owear-native.d.ts`.

## Configuration files

| File | Purpose |
|---|---|
| `vite.config.ts` | Adds `@owear/vite-plugin`; sets `base: './'` for `app://` |
| `package.json` | Scripts `dev`/`build` call the `ow` CLI |
| `tsconfig.json` | TypeScript for both renderer and main |
| `owear.bridge.ts` | Optional: app ↔ installer contract (see [Installers](../guides/installers.md)) |
| `owear.pack.json` | Optional: packaging overrides (`modes.default`, etc.) |

## What lives outside the project

`ow dev` and `ow build` keep intermediate artifacts in `.owear/` (git-ignored):
the compiled main process, compiled workers, app bundles, and installer staging.

## Next steps

- [Dev and build](dev-and-build.md) — what each command produces.
- [Main process](../guides/main-process.md) — the Node sidecar in depth.
