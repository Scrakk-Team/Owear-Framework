---
title: Kernel and lifecycle
description: The kernel is the native owear binary. This page describes its bootstrap and
order: 2
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Kernel and lifecycle

The kernel is the native `owear` binary. This page describes its bootstrap and
the invariants that keep multi-platform behavior predictable.

## Bootstrap

1. **Platform init** — GTK / Win32 is started.
2. **Module loading** — builtins are registered (`RegisterGeneratedBuiltins()`);
   dynamic `.owm` modules are discovered (`ModuleLoader::LoadAll()` scans
   `OW_MODULES_DIR` and `<exe>/modules`, non-recursively).
3. **Bridge/WebView setup** — the `app://` scheme is registered and the bridge
   script is prepared for injection.
4. **`App::OnReady` callbacks** run — this is where native-first apps create
   windows.
5. **Main loop** — runs until `Quit`.

`App::Main(argc, argv, AppOptions{ .id, .name, .version })` is the entry point for
native-first apps. `App::Post(fn)` queues a callback onto the main loop from any
thread; `App::Quit(code)` stops the loop.

## The window manager

Live windows are tracked in a `LiveWindows` map (`id → Window*`). Each `Window`
owns a platform `Impl` and a WebView backend. The `WindowId` is the key used by
the bridge, the control protocol, and modules.

## The three rules that keep platforms working

These invariants are enforced by the build and by review:

1. **Public contracts live in `include/ow/`.** All platforms and user modules
   include them. Breaking a signature fails compilation everywhere — intentional.
2. **One implementation per platform, selected by CMake, never `#ifdef`.** Files
   end in `_win.cpp` or `_linux.cpp`; the `CMakeLists.txt` picks them.
   Do not branch on platform inside a common file.
3. **All UI work goes through the main loop via `App::Post`.** This is what makes
   it safe for a module with its own thread to touch windows. `App::Post` is
   thread-safe; the rest of the window API is not.

A fourth rule applies to the module ABI: see
[Native module ABI](native-module-abi.md).

## Threading

- The main loop thread owns the UI.
- Module functions are invoked from the bridge on the appropriate thread; a
  module that starts its own worker must marshal UI work back through the host
  (`g_host->emit_event` is safe; direct window access is not).
- The control server handles NDJSON on its own, dispatching window operations
  through the main loop.

## Build selection

CMake presets: `linux-debug`, `linux-release`, `windows-release`.
Per-platform source selection uses the suffix convention. Generated discovery
(`api/generated.cmake`,
`builtins.generated.cmake`, `BuiltinRegistry.generated.cpp`) is produced by
`tools/gen-apis.mjs` and checked in CI with `--check`.

## Related

- [Architecture overview](overview.md)
- [Bridge](bridge.md)
- [Architecture rules](../contributing/architecture-rules.md)
