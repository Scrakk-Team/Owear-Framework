---
title: Native addons (N-API)
description: Owear does not embed its own Node engine. The main process runs on a real
order: 13
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Native addons (N-API)

Owear does **not** embed its own Node engine. The main process runs on a real
Node resolved by the Runtime Manager (`app.ensureNodeRuntime()`), with priority
`OW_NODE_BIN → system Node → Owear cache → official download`. That is why N-API
addons work with no compatibility layer.

## What works as-is

- Addons built with **N-API** (`node-addon-api`, `napi-rs`, `prebuildify`): the
  ABI is stable across Node versions, so they load without rebuilding.
- Packages that ship **Node prebuilds** — most do: `better-sqlite3`, `sharp`,
  `sqlite3`, `keytar`, and so on.
- Standard Node built-ins: `child_process`, `worker_threads`, `net`, `crypto`,
  `fs`, and the rest.
- Owear workers (`app.forkWorker`) run on the same Node, so they can load addons
  too.

## What does not work without a rebuild

- Binaries precompiled **for Electron** (what `electron-rebuild` /
  `@electron/rebuild` produce). Electron has its own ABI, which Node does not
  share. If you are coming from Electron, rebuild against Node:

  ```bash
  npm rebuild
  # or, against a specific Node:
  npm_config_runtime=node npm_config_target=$(node -p process.versions.node) npm rebuild
  ```

- Code that assumes Electron APIs (`electron`, `app`, `process.parentPort`,
  `utilityProcess`). Owear shims `process.parentPort` for workers, but
  `require('electron')` does not exist. Port those parts to `@owear/core`.

## Example: an N-API addon from the main process

```ts
import { app } from '@owear/core'
import Database from 'better-sqlite3'     // N-API prebuild

app.whenReady().then(() => {
  const db = new Database('cache.db')
  db.exec('create table if not exists kv (k text primary key, v text)')
})
```

`better-sqlite3` installs its Node prebuild and loads directly; the kernel is not
involved.

## Packaging notes

The Node runtime is resolved **at the destination** — you never ship a Node per
app. The `.node` files your app installs live in `node_modules/` and travel with
the app. To distribute:

1. Build/install against a Node compatible with the one the user will resolve
   (N-API makes this a non-issue).
2. Do not bundle Electron binaries.
3. If the addon ships per-platform prebuilds, include the target one
   (`linux-x64`, `win32-x64`).

For a single-binary app, the CLI bundles your main process with its dependencies
so `node_modules` is not required at runtime.

## Workers and addons

```ts
const w = app.forkWorker('heavy-worker.js')
w.postMessage({ op: 'init' })
```

The worker uses the same Node and can `require('sharp')` or load its own `.node`.

## Next steps

- [Main process](main-process.md) — `ensureNodeRuntime` and workers.
- [Native modules](native-modules.md) — C++ modules inside the kernel (a different
  mechanism from N-API addons).
