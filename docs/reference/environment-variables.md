---
title: Environment variables
description: The kernel, the CLI, and the SDK read these OW variables. ow dev and
order: 4
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Environment variables

The kernel, the CLI, and the SDK read these `OW_*` variables. `ow dev` and
`ow build` set most of them for you.

## Kernel

| Variable | Effect |
|---|---|
| `OW_APP_MAIN` | Path to the compiled Node sidecar entry (Electron-like mode). `ow dev`/`ow build` compile `app/main.ts` and point here. |
| `OW_NODE_BIN` | Path to a specific `node`. Has absolute priority; ignored with a warning if unusable. |
| `OW_DEV_SERVER_URL` | Initial URL for windows in dev. |
| `OW_START_URL` | Initial URL when there is **no** main process. |
| `OW_CONTROL_SOCKET` | Control socket path (set for the sidecar). |
| `OW_MODULES_DIR` | `path.delimiter`-separated list of directories containing `.owm` files. The loader does **not** recurse. |
| `OW_ASSETS_DIR` | Root of the `app://` scheme (default `./dist`). |
| `OW_APP_WORKERS` | Directory of compiled workers, exposed via `app.workersDir()`. |
| `OW_DEMO` | Opens the native demo window. |
| `OW_APP_NAME` / `OW_APP_ID` | App identity used for paths, registry, and safe storage. |
| `OW_MODE` | `app` \| `installer` \| `uninstaller` (set by the embedded payload). |
| `OW_APP_VERSION` | Version the installer is installing (from `installer.json`). |
| `OW_CLOSE_TIMEOUT_MS` | Timeout for the close veto (default 1000). |
| `OW_DEBUG` | Enable verbose kernel logging where available. |

## CLI

| Variable | Effect |
|---|---|
| `OW_KERNEL_BIN` | Path to the `owear` binary. |
| `OW_MODULES_OUT` | Output directory for compiled `.owm` (`owear-build-native`). |
| `OW_INCLUDE_DIR` | Framework headers for `native/*.cpp`. |
| `OW_SIGN_KEY` | Ed25519 private key for signing artifacts. |
| `OW_SIGN_PFX` | PKCS#12 certificate for Authenticode. |
| `OW_SIGN_PFX_PASSWORD` | Password for the PFX. |
| `OW_SIGN_TIMESTAMP` | Default Authenticode timestamp URL. |

## Platform

| Variable | Platform | Effect |
|---|---|---|
| `XDG_RUNTIME_DIR` | Linux | Control/lock sockets. |
| `XDG_CACHE_HOME` / `XDG_DATA_HOME` / `XDG_CONFIG_HOME` | Linux | Cache, data, config roots (paths, safe storage, registry). |
| `XDG_DOWNLOAD_DIR` | Linux | Downloads path override. |
| `APPDATA` / `LOCALAPPDATA` | Windows | Roaming/local app data. |

## Notes

- `OW_MODULES_DIR` is the most common source of "module not found": the native
  loader scans a directory **non-recursively**, so each module must be in its own
  listed directory (or a flat directory of `.owm` files).
- The SDK throws `Owear: OW_CONTROL_SOCKET not set` when neither the variable nor
  a live socket in `XDG_RUNTIME_DIR` is found — usually because the app was
  started without the CLI.
