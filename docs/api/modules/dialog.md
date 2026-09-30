---
title: dialog
description: Native file pickers and message boxes.
order: 6
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `dialog`

Native file pickers and message boxes.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

Two styles are exposed: a raw positional API and an Electron-style options API
(wrapped by the SDK's `dialog`, see [`dialog` SDK](../../api/sdk/dialog.md)).

## Raw API

### `open`

```ts
open(mode, title?, defaultPath?, filters?) → string | string[] | null
```

- `mode`: `'open' | 'multi' | 'save' | 'dir'`.
- Returns `null` on cancel; an array for `'multi'`.

```ts
const path = await ow.invoke<string | null>('dialog', 'open', 'open', 'Open a file')
const files = await ow.invoke<string[] | null>('dialog', 'open', 'multi', 'Open many')
```

### `messageBox`

```ts
messageBox(type, title, message, buttons?) → number
```

- `type`: `'info' | 'warning' | 'error' | 'question'`.
- `buttons`: array of labels; returns the index of the pressed button.

```ts
const resp = await ow.invoke<number>('dialog', 'messageBox', 'info', 'Title', 'Body', ['OK'])
```

## Electron-style API

### `showOpenDialog`

```ts
showOpenDialog({ title?, defaultPath?, buttonLabel?, filters?, properties? })
  → { canceled: boolean, filePaths: string[] }
```

`properties`: `openFile | openDirectory | multiSelections | showHiddenFiles |
createDirectory`.

### `showSaveDialog`

```ts
showSaveDialog({ title?, defaultPath?, buttonLabel?, filters? })
  → { canceled: boolean, filePath: string }
```

### `showMessageBox`

```ts
showMessageBox({ type?, title?, message, detail?, buttons?, defaultId?, cancelId?, checkboxLabel? })
  → { response: number, checkboxChecked: boolean }
```

## Notes

- Linux: `GtkFileChooserNative`/`GtkMessageDialog`. Dialogs run on the GTK main
  thread and are safe to invoke synchronously.
- Windows: `IFileDialog`.
- Modal dialogs cannot be automated in CI (`VERIFY ON REAL DESKTOP`).

## See also

- [Dialogs and notifications guide](../../guides/dialogs-and-notifications.md)
- [`dialog` SDK](../../api/sdk/dialog.md)
