---
title: app (native)
description: Application lifecycle helpers coupled to the kernel.
order: 2
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `app` (native)

Application lifecycle helpers coupled to the kernel.

- **Kind:** builtin
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

> Do not confuse this with the SDK `app` object. This is the low-level module;
> the SDK wraps it.

## Functions

| Function | Signature |
|---|---|
| `setBadgeCount` | `(count) → null` |
| `requestSingleInstanceLock` | `() → boolean` |
| `relaunch` | `() → null` (does not return) |

### `setBadgeCount`

Sets the launcher badge count using the Unity LauncherEntry D-Bus interface. On
desktops without it (most non-Unity), it returns a clear
`badge not supported on this desktop` error.

### `requestSingleInstanceLock`

Returns `true` for the first instance and `false` if another instance already
holds the lock. The first instance binds a per-app socket; a subsequent instance
connects, forwards its `argv`, and exits. The first instance then emits
`secondInstance` with `{ argv }`:

```ts
const gotLock = await invokeNative<boolean>('app', 'requestSingleInstanceLock')
app.on('second-instance', ({ argv }) => focusMainWindow())
```

The lock socket lives at `$XDG_RUNTIME_DIR/owear-single-<appId>.sock` (or
`/tmp` fallback).

### `relaunch`

Re-execs the current binary with the same `argv` using `execv`. It responds
before executing, so the caller should not expect the process to continue.

## See also

- [`app` SDK](../../api/sdk/app.md)
- [Main process guide](../../guides/main-process.md)
