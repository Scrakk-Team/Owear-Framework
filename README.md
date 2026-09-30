<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Owear

Native desktop framework for **Linux and Windows**. **No bundled browser**: it
uses the operating system's own WebView (WebView2 on Windows, WebKitGTK on
Linux).

```
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

## Why not Electron/Tauri

| | Electron | Tauri | **Owear** |
|---|---|---|---|
| Core RAM | ~150–200 MB | ~30–60 MB | **~5–10 MB** |
| Embedded Node | yes (pinned V8+Node) | no | **no — sidecar auto-installed on demand** |
| IPC | preload + pipe + V8 serialize | JSON-RPC | **direct host object + raw binary** |
| Modules | all loaded | all | **dlopen only what you use (.owm)** |

## Quick start

> **Status:** the `@owear/*` packages are not published on npm yet. The Linux
> runtime (`@owear/linux-x64-gnu`) is already packaged by `tools/pack-runtime.mjs`
> (kernel + stock modules + headers) and ready to publish; Windows is
> packaged from native runners. Until then, work from a monorepo checkout:
> `ow dev` compiles the kernel the first time (it needs the system dependencies
> from `.github/workflows/ci.yml`).

```bash
# from an Owear checkout
pnpm install
node packages/cli/src/ow.js create my-app   # scaffold from the template
```

The published flow (pending release) is the usual one:
`pnpm dlx @owear/cli create my-app && cd my-app && pnpm install && pnpm dev`.

Write native C++ next to your frontend:

```cpp
// native/files.cpp
#include <ow/Json.h>
#include <ow/Module.h>

static void readText(const ow_request_t* req, ow_response_t* res) {
    // JSON args → JSON response. No exceptions across the host boundary.
}

OW_MODULE_BEGIN(files, "1.0.0")
OW_FN(readText)
OW_MODULE_END()
```

Call it from the renderer with generated types:

```ts
import { files } from '@owear/native'
const txt = await files.readText('/etc/hostname')
```

Or drive the app from the main process, Electron style:

```ts
// app/main.ts (Node sidecar)
import { app, BrowserWindow } from '@owear/core'

app.whenReady().then(() => {
  new BrowserWindow({ width: 1200, height: 800, titleBarStyle: 'custom' })
})
```

## Building the framework (from this repo)

```bash
cmake --preset linux-release        # or: windows-release
cmake --build --preset linux-release
ctest --test-dir build/linux-release --output-on-failure
```

## Structure

- `include/ow/` — public contracts (changing them breaks every platform: anti-drift)
- `src/<Module>/<file>_<platform>.cpp` — one implementation per platform, selected by CMake
- `api/<name>/` — **one folder per API** with its `owear.module.json` manifest
  (single source of truth). See [manifests](docs/reference/manifests.md)
- `tools/gen-apis.mjs` — generates CMake discovery and the builtin registry from the manifests
- `packages/` — npm SDK (`@owear/core`, `@owear/cli`, `@owear/vite-plugin`)
- `docs/` — [full documentation](docs/README.md): guides, SDK, modules and architecture

## Contributing

Architecture rules, how to add an API, and how to write E2E tests:
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

Apache License 2.0 — see [LICENSE](LICENSE).
