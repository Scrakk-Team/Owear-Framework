---
title: 0.1.1
description: First release distributed through npm, with the modular API registry and the Linux runtime as a package.
order: 2
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.1

First release distributed through npm, with the modular API registry and the
Linux runtime as a package.

## Added

- **Per-API manifests** (`api/<name>/owear.module.json`) as the single source of
  truth: dynamic discovery in CMake and generated builtin registration
  (`tools/gen-apis.mjs`). Adding an API no longer requires editing lists by hand.
- **Registry with metadata**: `module.list` / `module.info` (version, origin,
  builtin, functions) and `listNativeModules()` / `nativeModuleInfo()` in the SDK.
- **Runtime package `@owear/linux-x64-gnu`**: native kernel + stock modules
  (`.owm`) + headers, npm-ready (`tools/pack-runtime.mjs`).
- **Professional Starter** in `ow create`: custom titlebar, native-module demos
  and app-name substitution.
- **CLI**: `ow api list` and `ow api new`.
- **Examples**: `examples/starter`, `examples/cursor-xray`, `examples/snake`.
- **Tooling**: `ow api` and CI checks (`gen-apis --check`, `check-apis`,
  `license-header --check`, `version check`).

## Changed

- **Linux windows**: rounded corners and **native edge resize** on undecorated
  windows (GDK filter + RGBA window + transparent WebView).
- **Cascading Node runtime resolution**: `OW_NODE_BIN` → system Node → cache →
  download, logging the origin (`env|system|cache|downloaded`).
- The close timeout (`closeRequested`) is configurable via
  `OW_CLOSE_TIMEOUT_MS` (default 1000 ms).
- **Apache-2.0** license with an SPDX header across all code.

## Fixed

- `ow dev` now **loads the stock modules** and, inside the monorepo, uses the
  freshly built kernel instead of the npm package.
- `esbuild` resolution to compile `app/main.ts` (direct dependency).
- Reliability of the **control socket on Windows** (pipe, `app.quit`,
  `ImpersonateNamedPipeClient`) and of **E2E on macOS/Linux**.
