---
title: 0.1.2
description: Cross-platform distribution release with the Windows runtime as a package.
order: 3
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.2

Cross-platform distribution release with the Windows runtime as a package.

## Added

- **Windows runtime `@owear/win32-x64`**: kernel + 15 stock modules (`.dll`) +
  headers, **self-contained** (it bundles `zlib`, OpenSSL and the MSVC runtime
  `VCRUNTIME`/`MSVCP`, so no VC++ Redistributable is needed).
- `@owear/win32-x64` added as an **optional dependency** of `@owear/cli` (it only
  installs on Windows x64).
- `Build runtime packages` workflow that builds the per-platform runtimes on
  native runners and publishes them as artifacts.
- `tools/windows/setup.ps1` + `docs/windows.md`: minimal setup on Windows.

## Fixed

- Runtime packaging now copies **all** dependency DLLs (`z.dll`, `libssl`/`libcrypto`,
  MSVC runtime) **next to `owear.exe`**.
- Support for the MSVC **multi-config layout** (`src/Release/owear.exe`,
  `api/<x>/Release/<x>.dll`) in `tools/pack-runtime.mjs`.
