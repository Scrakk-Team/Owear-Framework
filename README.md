<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

<p align="center"><img src="assets/logo.svg" width="96" alt="Owear"></p>

# Owear

Native desktop framework for **Linux and Windows**. **No bundled browser**: it
uses the operating system's own WebView (WebView2 on Windows, WebKitGTK on
Linux).

```
┌─────────────────────────────────────────────┐
│  App process (native kernel)                │
│  kernel: app · windows · bridge · loader    │
│  .owm modules: fs · dialog · yours          │
└──────────────┬──────────────────────────────┘
               │ direct bridge (no Node in between)
     OS WebView  ←  your frontend (Vite/React/…)
               │
     Node sidecar (auto-installed, optional)
       └── app/main.ts — Electron-like API
```

## Benchmarks

Real numbers, measured with our own harness (`benchmarks/`) on the same app and
machine (Linux, Xvfb, software rendering; medians of 3 interleaved runs, memory
in **PSS**). Full data, methodology and charts: [`benchmarks/`](benchmarks/README.md).

| Metric | Owear | Electron | Tauri | Neutralino |
|---|---:|---:|---:|---:|
| Startup (ms) | 1,438 | 1,028 | 1,001 | 958 |
| Idle RAM (MB, PSS) | **105** | 248 | 120 | 129 |
| Peak RAM (MB, PSS) | 334 | 408 | 255 | 308 |
| Footprint (MB) | **1.8** | 262 | 11.6 | 3.0 |
| Sequential IPC (ops/s) | 1,496 | **3,192** | 1,127 | 23 |
| Concurrent IPC (ops/s) | 4,211 | **10,449** | 2,886 | 876 |
| Payload 1 MB (ms) | 28 | **11.5** | 27 | 111 |
| Native → render events (ev/s) | **250,000** | 34,990 | 1,371 | 10,438 |
| JS loop 20M (ms) | 27 | 32 | 24 | 24 |

**Owear's net score vs Electron: 70%** (geometric mean of 11 zones; lower is
better). Owear wins on **memory (~2.4× less idle), footprint (~140× smaller) and
native → renderer events (~7× faster)**; it is behind on **IPC and startup**.

> Electron bundles Chromium; Owear/Tauri/Neutralino use the OS WebView. Neutralino
> is measured through its own extension. See the caveats in
> [`benchmarks/README.md`](benchmarks/README.md).

## Quick start

The CLI is published on npm as `@owear/cli` (**0.1.4**):

```bash
pnpm dlx @owear/cli create my-app    # or: npm i -g @owear/cli && ow create my-app
cd my-app
pnpm install
pnpm dev                             # or: npm run dev
```

You get a working **Starter**: a Vite + TypeScript frontend, a Node main process,
a custom title bar, and demos of most native APIs. The two files you edit first:

- `src/renderer.ts` — the UI. It runs inside the WebView and calls native code
  through `window.ow`:

  ```ts
  const hostname = await window.ow.invoke<string>('fs', 'readText', '/etc/hostname')
  ```

- `app/main.ts` — the Node main process. It creates windows and exposes Node to
  the UI:

  ```ts
  import { app, BrowserWindow } from '@owear/core'

  app.whenReady().then(() => {
    const win = new BrowserWindow({ title: 'My App', width: 900, height: 600 })
    win.loadURL(process.env.OW_DEV_SERVER_URL!)
  })
  ```

Then build:

```bash
pnpm build        # frontend + main.js + modules → dist/
ow build app      # a single self-contained binary → release/
```

Full walkthrough: [Quick start](docs/getting-started/quickstart.md) ·
[Your first app](docs/getting-started/your-first-app.md).

Working on the **framework** itself (from a checkout):

```bash
pnpm install
pnpm build:native                           # build the kernel for your platform
node packages/cli/src/ow.js create my-app   # scaffold from the template
```

## Native when you need it

Most desktop features are **built in** (`fs`, `dialog`, `menu`, `tray`, `screen`…),
so you write TypeScript and call them directly — no native code needed:

```ts
const entries = await window.ow.invoke('fs', 'readDir', '/etc')
```

When a hot path really needs native code, drop C++ in `native/`: the CLI compiles
it to a `.owm` module and `@owear/vite-plugin` generates typed bindings.

```ts
import { files } from '@owear/native'
const text = await files.readText('/etc/hostname')
```

See [Native modules](docs/guides/native-modules.md).

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
