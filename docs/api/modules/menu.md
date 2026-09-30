---
title: menu
description: Context menus from a declarative JSON template. Menu clicks are emitted as
order: 10
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `menu`

Context menus from a declarative JSON template. Menu clicks are emitted as
`menu.click`.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

## Functions

| Function | Signature |
|---|---|
| `popup` | `({ items, x?, y? }) → null` |
| `setApplicationMenu` | `({ items }) → null` (no-op on Linux) |

### `popup`

```ts
popup({ items: MenuItem[], x?: number, y?: number }) → null
```

The item shape is the same template used by the SDK `Menu`:

```ts
interface MenuItem {
  id?: string
  label?: string
  role?: string
  type?: 'normal' | 'separator' | 'submenu' | 'checkbox' | 'radio'
  checked?: boolean
  enabled?: boolean
  visible?: boolean
  accelerator?: string
  submenu?: MenuItem[]
}
```

On Linux the popup is positioned at the current pointer because the invocation
arrives asynchronously from the renderer (there is no native trigger event).

### `setApplicationMenu`

```ts
setApplicationMenu({ items }) → null
```

On Linux this is a **no-op by design** — GNOME has no global menubar. Windows
maps it to a native `HMENU`.

## Events

When an item is activated, the kernel emits `menu.click`:

```ts
{ id: string, role: string, windowId: number }
```

The event is delivered to the main process (SDK) and to windows.

## Notes

- Prefer the SDK (`Menu.buildFromTemplate(...).popup(...)`), which handles id
  registration and click dispatch for you.
- Accelerators in a popup are shown as text; global behavior depends on the host.

## See also

- [Menus and tray guide](../../guides/menus-and-tray.md)
- [`Menu` SDK](../../api/sdk/menu.md)
