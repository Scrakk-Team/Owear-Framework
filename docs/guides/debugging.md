---
title: Debugging
description: Open the inspector for a window from the renderer 
order: 3
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Debugging

## Developer tools

Open the inspector for a window from the renderer:

```ts
await ow.invoke('window', 'openDevTools', window.__owWindowId)
```

or from the main process:

```ts
win.webContents.openDevTools()
```

You can also trigger it from a menu item with the `toggleDevTools` role.

## Logs

- The kernel writes to its stdout/stderr; `ow dev` shows them inline because the
  CLI runs the kernel with inherited stdio.
- Native modules can log through the host (`g_host->log`) and via `ow::log` in
  the kernel.
- The Node sidecar logs to the same console.

Set `OW_DEBUG=1` for verbose kernel logging where available.

## Crash reports

Install the crash reporter to capture native crashes (segfaults, aborts) with a
backtrace:

```ts
const dir = await ow.invoke<string>('crashreporter', 'install')
// logs go to $XDG_CACHE_HOME/owear/crashes/crash-<pid>.log

const last = await ow.invoke<string | null>('crashreporter', 'lastCrashLog')
```

It handles `SIGSEGV`, `SIGABRT`, `SIGFPE`, `SIGBUS`, and `SIGILL`, writes the
signal + `backtrace()` frames, then re-raises the signal so a core dump is still
produced. See [`crashreporter`](../api/modules/crashreporter.md).

## Introspecting modules

```ts
import { listNativeModules, nativeModuleInfo } from '@owear/core'

const mods = await listNativeModules()
// [{ name, version, origin, builtin, functions, functionNames }]
```

From the renderer you can reach the same data through the control protocol; but
`listNativeModules` in the main process is the easy path. If a module is missing,
check `OW_MODULES_DIR` and whether its optional system dependencies were present
at build time.

## Common problems

| Symptom | Likely cause / fix |
|---|---|
| `OW_CONTROL_SOCKET not set` | You ran the kernel directly instead of `ow dev`/`ow build`; set the variable or use the CLI |
| `BrowserWindow created before app.whenReady()` | Move window creation inside the `whenReady().then(...)` callback |
| `ow: timeout <mod>/<fn>` | The module is not loaded, or the native function blocked. Check `OW_MODULES_DIR` |
| Window opens but blank | Dev server was not ready, or `base` in `vite.config.ts` is not `'./'` |
| Module not found on Linux | Stock modules live in per-directory paths and the loader does not recurse; ensure `OW_MODULES_DIR` lists each module directory |
| Dialogs/tray don't appear in CI | They need a real desktop; these are `VERIFY-ON-REAL-DESKTOP` |
| `globalShortcut` fails | You are on Wayland; X11 only |

## Running the app against a built bundle

```bash
ow build
OW_ASSETS_DIR=dist OW_APP_MAIN=dist/main.js OW_MODULES_DIR="…" ./owear
```

`ow build` prints this exact line with your paths filled in.

## Next steps

- [Testing](../architecture/testing.md) — unit and E2E suites.
- [Environment variables](../reference/environment-variables.md).
