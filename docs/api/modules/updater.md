---
title: updater
description: Native primitives for auto-update  state inspection and atomic apply + relaunch.
order: 23
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `updater`

Native primitives for auto-update: state inspection and atomic apply + relaunch.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

The high-level logic (feed, delta, signatures) lives in the SDK's
[`autoUpdater`](../../api/sdk/auto-updater.md). This module only does what must
run in the kernel.

## Functions

| Function | Signature |
|---|---|
| `checkForUpdates` | `(feedUrl, currentVersion) → { updateAvailable, version, notes? }` |
| `downloadUpdate` | `() → path` |
| `installAndRelaunch` | `() → null` (does not return on success) |
| `state` | `() → { version, exe, dir?, mode?, platform, arch, hasRollback }` |
| `apply` | `({ path, backup? }) → null` (atomic replace + relaunch) |
| `rollback` | `() → null` (restore previous + relaunch) |
| `commit` | `() → null` (delete the backup) |

## Behavior

- `apply` copies the current binary to `<exe>.owprev` (unless `backup: false`),
  replaces the executable atomically, and re-execs. On success it does not
  return: the connection drops and the new process starts.
- `rollback` restores `<exe>.owprev`. It fails if no pending backup exists.
- `commit` deletes the backup, disarming rollback.
- `state` reads the installed-app registry to report the install directory and
  mode.

## Legacy JSON manifest

`checkForUpdates`/`downloadUpdate`/`installAndRelaunch` implement the simpler JSON
manifest flow (`{ version, url, sha256, notes }`) and verify the SHA-256 when
present. For the modern YAML + delta + signature flow, use `autoUpdater`.

## Safety

- SHA-256 is always verified for downloaded artifacts.
- The SDK additionally verifies Ed25519 signatures when a public key is
  configured, and supports delta downloads, resume, and the boot guard.

## See also

- [Auto-update guide](../../guides/auto-update.md)
- [`autoUpdater` SDK](../../api/sdk/auto-updater.md)
