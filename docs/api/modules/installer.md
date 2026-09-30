---
title: installer
description: Installer/uninstaller operations on the embedded payload.
order: 9
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `installer`

Installer/uninstaller operations on the embedded payload.

- **Kind:** builtin
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

Available only in binaries built in installer/uninstaller mode (`OW_MODE=installer`
or `uninstaller`). Prefer the SDK wrapper [`installer`](../../api/sdk/installer.md).

## Functions

| Function | Signature |
|---|---|
| `mode` | `() → 'app' \| 'installer' \| 'uninstaller'` |
| `info` | `() → InstallerInfo` |
| `bridge` | `() → OwearBridge \| null` |
| `payloadList` | `() → PayloadEntry[]` |
| `payloadRead` | `({ path }) → { path, data, size }` |
| `defaultDir` | `() → string` |
| `chooseDir` | `({ title?, defaultPath? }) → string \| null` |
| `plan` | `({ mode?, layout?, order? }) → PayloadEntry[]` |
| `install` | `({ dir, mode?, layout?, order?, uninstaller?, publisher? }) → InstallResult` |
| `uninstall` | `({ dir, keepData? }) → UninstallResult` |
| `verify` | `({ dir }) → { ok, mismatches }` |
| `state` | `({ dir? }) → StateResult` |
| `list` | `() → object[]` |
| `shortcuts` | `(ShortcutOptions) → boolean` |
| `launch` | `({ path, args? }) → boolean` |
| `elevate` | `() → { elevated: boolean }` |

## Payload entries

```ts
interface PayloadEntry { rel: string; dst: string; size: number; dir: boolean }
```

`rel` is a path inside the embedded payload; `dst` is the destination relative
to the install directory.

## Modes

- `minimal` — a single app binary;
- `layout` — a directory tree (kernel, assets, modules, main process, workers).

## Example

```ts
const plan = await ow.invoke('installer', 'plan', { mode: 'layout' })
const result = await ow.invoke('installer', 'install', { dir: '/home/me/.local/opt/notes', mode: 'layout' })
await ow.invoke('installer', 'shortcuts', { execPath: '/home/me/.local/opt/notes/notes' })
await ow.invoke('installer', 'launch', { path: '/home/me/.local/opt/notes/notes' })
```

## Registry

Installed apps are recorded in `$XDG_CONFIG_HOME/owear/installed/<appId>.json`
(Linux) or `%APPDATA%\owear\installed\<appId>.json` (Windows). The updater reads
the same registry.

## See also

- [Installers guide](../../guides/installers.md)
- [`installer` SDK](../../api/sdk/installer.md)
