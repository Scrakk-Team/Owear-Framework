---
title: tray
description: System tray icon with a context menu.
order: 22
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `tray`

System tray icon with a context menu.

- **Kind:** module (`.owm`), **optional**
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

Optional because it needs a StatusNotifierWatcher (Linux) or equivalent; it is
omitted from builds without the dependencies.

## Functions

| Function | Signature |
|---|---|
| `create` | `(id) → null` |
| `setImage` | `(pngBase64) → null` |
| `setPressedImage` | `(pngBase64) → null` |
| `setToolTip` | `(text) → null` |
| `setTitle` | `(text) → null` |
| `setContextMenu` | `({ items }) → null` |
| `popupContextMenu` | `() → null` |
| `destroy` | `() → null` |

## Events

| Event | Payload |
|---|---|
| `tray.event` | `{ button: 'left' \| 'right' \| 'middle' }` |
| `menu.click` | `{ id, role, windowId }` (from the context menu) |

## Platform notes

- Linux: a hand-rolled `org.kde.StatusNotifierItem` + `com.canonical.dbusmenu`
  implementation over GDBus (no libappindicator). Works on GNOME (appindicator
  extension), KDE, and XFCE (SNI plugin).
- `setPressedImage` is a no-op on Linux.
- Windows (`Shell_NotifyIcon`) is the intended backend; verify in CI.

## Prefer the SDK

Use [`Tray`](../../api/sdk/tray.md) in the main process: it converts images to
PNG base64, wires events, and reuses `Menu` for the context menu.

## See also

- [Menus and tray guide](../../guides/menus-and-tray.md)
