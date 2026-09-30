---
title: globalshortcut
description: System-wide keyboard shortcuts.
order: 8
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `globalshortcut`

System-wide keyboard shortcuts.

- **Kind:** module (`.owm`), **optional**
- **Version:** 0.1.0
- **Platforms:** Linux (X11), Windows

Because it is optional, the module is omitted from builds where its system
dependencies are unavailable.

## Functions

| Function | Signature |
|---|---|
| `register` | `(accelerator) → bindingId` |
| `unregister` | `(bindingId) → null` |

## Accelerators

Grammar: modifiers `Ctrl`/`Control`, `Shift`, `Alt`, `Super`/`Meta`, plus a key
understood by `XStringToKeysym`, joined with `+`.

```ts
const id = await ow.invoke<number>('globalshortcut', 'register', 'Ctrl+Shift+Space')
// …
await ow.invoke('globalshortcut', 'unregister', id)
```

## Events

```ts
{ id: number, accelerator: string }
```

Emitted as `globalShortcut.press` when the combination is pressed.

```ts
ow.on('globalShortcut.press', ({ id, accelerator }) => focusWindow())
```

## Platform notes

- Linux: `XGrabKey` on the X11 root window, trying NumLock/CapsLock variants, and
  a GDK filter to detect presses. Under **Wayland** `register` returns
  `globalShortcut requires an X11 session`.
- Windows: `RegisterHotKey` (verify in CI).

## See also

- [Global shortcuts guide](../../guides/global-shortcuts.md)
