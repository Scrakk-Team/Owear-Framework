---
title: Dev and build
description: This page explains what ow dev and ow build actually do, so you can reason
order: 1
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Dev and build

This page explains what `ow dev` and `ow build` actually do, so you can reason
about artifacts, environment variables, and hot reload.

## `ow dev`

`ow dev` orchestrates three moving parts:

```text
Vite dev server ──► http://localhost:5173
        │
        │  OW_DEV_SERVER_URL
        ▼
Native kernel (owear) ── loads your frontend, injects window.ow
        ▲
        │  OW_APP_MAIN (optional)
Node sidecar (app/main.js, compiled from app/main.ts)
```

Steps:

1. **Vite** starts with `--port 5173 --strictPort`. The CLI waits until it
   responds before continuing.
2. **Native modules** — if `native/` exists, `owear-build-native` compiles every
   `.cpp` into `.owear/modules/<name>.owm`.
3. **Main process** — `app/main.ts` is compiled with esbuild to
   `.owear/main.js`. Dependencies stay external (they are in `node_modules`).
4. **Workers** — `app/workers/**` are compiled to `.owear/workers/`.
5. **Kernel** — launched with the environment below.

Environment passed to the kernel:

| Variable | Value |
|---|---|
| `OW_APP_NAME` | `name` from `package.json` |
| `OW_DEV_SERVER_URL` | `http://localhost:5173/` |
| `OW_APP_MAIN` | `.owear/main.js` (if a main entry exists) |
| `OW_START_URL` | dev URL (only when there is no main entry) |
| `OW_APP_WORKERS` | `.owear/workers` (if workers exist) |
| `OW_MODULES_DIR` | stock modules path + `.owear/modules` |

> The stock modules directory matters: the kernel does **not** recurse into
> subdirectories, so the CLI passes each `build/<preset>/api/<name>/` directory
> joined with `path.delimiter`.

Press `Ctrl+C` or close the last window: the CLI kills the kernel and Vite.

## `ow build` (the bundle)

```bash
ow build
```

Produces `dist/`:

- `dist/` — Vite production build of the frontend;
- `dist/main.js` — the main process, with `@owear/core` **bundled** in (a
  single-file app must not depend on `node_modules` at runtime);
- `dist/workers/` — compiled workers;
- `dist/modules/` — compiled `.owm` modules.

The CLI prints the exact command to run the bundle against the kernel:

```bash
OW_ASSETS_DIR=dist OW_APP_MAIN=dist/main.js OW_MODULES_DIR="…" ./owear
```

## `ow build app`

Packages the app into a distributable artifact. See
[Packaging](../guides/packaging.md). Formats: `binary` (default), `deb`,
`appimage`, `msi`.

```bash
ow build app --format binary
ow build app --format deb
ow build app --format appimage
ow build app --format msi            # Windows
```

## `ow build installer` / `uninstaller`

Builds a separate installer (or uninstaller) app that embeds your app as a
payload. See [Installers](../guides/installers.md).

## Why the main process is compiled

Node does not run TypeScript until v22.6 (and only with a flag until v23.6). The
CLI uses esbuild (which ships with Vite) to compile `app/main.ts` to JavaScript so
that **any** Node on the user's machine can run it. This is what makes the
"resolve Node at runtime" model in [Native addons](../guides/native-addons.md)
work.

## Next steps

- [Your first app](your-first-app.md) — a complete tutorial.
- [Packaging](../guides/packaging.md) and [Installers](../guides/installers.md).
