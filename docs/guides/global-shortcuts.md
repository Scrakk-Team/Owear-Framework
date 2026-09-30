---
title: Global shortcuts
description: The globalshortcut module registers keyboard shortcuts that work even when your
order: 7
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Global shortcuts

The `globalshortcut` module registers keyboard shortcuts that work even when your
window is not focused. It is **optional** and X11-only on Linux: Wayland
compositors isolate global shortcuts, so there is no portable way to grab them.

```ts
const id = await ow.invoke<number>('globalshortcut', 'register', 'Ctrl+Shift+Space')
// later
await ow.invoke('globalshortcut', 'unregister', id)
```

## Registering

```ts
globalshortcut.register(accelerator): Promise<number>   // returns a bindingId
globalshortcut.unregister(bindingId): Promise<null>
```

Accelerator grammar:

- modifiers: `Ctrl` (or `Control`), `Shift`, `Alt`, `Super` (or `Meta`);
- a key name understood by `XStringToKeysym`, e.g. `P`, `F5`, `Space`, `Return`;
- joined with `+`, e.g. `Ctrl+Alt+T`, `Super+F1`.

On Linux the implementation grabs the key on the X11 root window with `XGrabKey`
(trying NumLock/CapsLock variants) and installs a GDK filter to detect presses.

## Receiving presses

When a binding fires, the kernel emits `globalShortcut.press`:

```ts
ow.on('globalShortcut.press', ({ id, accelerator }) => {
  // focus the window, toggle a panel, etc.
})
```

## Platform notes

- **Linux:** X11 only. Under Wayland, `register` returns
  `globalShortcut requires an X11 session`.
- **Windows:** `RegisterHotKey` (verify in CI).

Because it is marked optional, the module is omitted from builds where its system
dependencies are unavailable.

## Next steps

- [`globalshortcut` module reference](../api/modules/globalshortcut.md).
- [Menus and tray](menus-and-tray.md) — in-app accelerators via menu items.
