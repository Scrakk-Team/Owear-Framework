---
title: Module manifests
description: Every API lives in api/<name/ and is declared by a manifest,
order: 5
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Module manifests

Every API lives in `api/<name>/` and is declared by a manifest,
`owear.module.json`. That manifest is the **single source of truth**: CMake
discovery, runtime registration, the `ow api` CLI, and CI validation all derive
from it. Adding an API does **not** require editing lists by hand.

## Structure

```text
api/<name>/
├── owear.module.json     ← manifest (single source of truth)
├── CMakeLists.txt        ← build recipe for the module
├── README.md
└── src/
    ├── <name>.cpp              (cross-platform)
    ├── <name>_linux.cpp        (per platform)
    └── <name>_win.cpp
```

## Kinds: `module` and `builtin`

| kind | What it is | How it loads |
|---|---|---|
| `module` | Compiles to a standalone `<name>.so/.dll/.dylib` (`.owm`) | `dlopen` at runtime from `OW_MODULES_DIR` / `<exe>/modules` |
| `builtin` | Coupled to the kernel (needs `LiveWindow`, `ControlServer`, the WebView) | Linked into the binary; registered by `RegisterGeneratedBuiltins()` |

## Manifest fields

```jsonc
{
  "$schema": "../owear.module.schema.json",
  "name": "screen",
  "kind": "module",
  "version": "0.1.0",
  "description": "Monitors, primary display and global cursor position.",
  "platforms": ["linux", "win"],
  "optional": false,
  "functions": ["getAllDisplays", "getPrimaryDisplay", "getCursorScreenPoint", "watch", "unwatch"]
}
```

For a builtin:

```jsonc
{
  "name": "window",
  "kind": "builtin",
  "version": "0.1.0",
  "descriptors": [
    {
      "name": "ow-window",
      "factory": "WindowModuleDescriptorImpl",
      "core": true,
      "platforms": ["linux", "win"],
      "functions": ["minimize", "maximize", "close", "focus", "setTitle", "isMaximized", "respondCloseRequest"]
    }
  ],
  "sources": { "linux": ["src/window_extra.cpp"], "win": ["src/window_extra_win.cpp"] }
}
```

- `factory`: C++ function that returns the `ow_module_desc_t*`.
- `core: true`: the source lives in `src/Core/…` (not linked from `api/`).
- `optional: true`: may be omitted when system dependencies are missing.

The JSON Schema is at `api/owear.module.schema.json`; manifests reference it with
`"$schema"`.

## Code generation

```bash
node tools/gen-apis.mjs           # regenerate
node tools/gen-apis.mjs --check   # fail if out of date (CI)
node tools/check-apis.mjs         # manifest ↔ C++ consistency
```

Generated files (do not edit by hand):

| File | Contents |
|---|---|
| `api/generated.cmake` | `add_subdirectory(<name>)` for each `module`, included by `api/CMakeLists.txt` |
| `src/Core/builtins.generated.cmake` | builtin sources per platform (`OW_BUILTIN_SOURCES_<plat>`) |
| `src/Core/BuiltinRegistry.generated.cpp` | `RegisterGeneratedBuiltins()` registering each builtin descriptor |

`tools/check-apis.mjs` verifies that each C++ descriptor is named like the
manifest and that its functions match **exactly** (including table lambdas).

## Runtime registry

The kernel keeps a registry (`src/Bridge/Dispatcher.cpp`):

- `RegisterModule(desc, origin)` stores `{ name, version, origin, builtin, functions }`;
- dynamic modules are discovered by `ModuleLoader::LoadAll()` (directory scan +
  `dlopen` + `ow_module_descriptor`);
- renderer/Node query it over the control socket:
  - `module.list` → `[{ name, version, origin, builtin, functions, functionNames }]`
  - `module.info { name }` → one module, or an error.

From the SDK: `listNativeModules()` and `nativeModuleInfo(name)`.

## Add an API

```bash
ow api new my-api     # creates api/my-api/ + manifest + skeleton + regenerates
# edit api/my-api/src/my-api.cpp (and the manifest if you add functions)
cmake --build --preset linux-release
```

From the renderer: `await ow.invoke('my-api', 'ping')`.

See [Adding an API](../contributing/adding-an-api.md) for the full walkthrough.
