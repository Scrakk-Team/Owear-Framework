---
title: Dialogs and notifications
description: Two modules cover user-facing prompts  dialog (native file pickers and message
order: 4
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Dialogs and notifications

Two modules cover user-facing prompts: `dialog` (native file pickers and message
boxes) and `notification` (system notifications). Both are callable from the
renderer directly, and `dialog` also has a typed SDK wrapper.

## File dialogs

### SDK style (recommended)

```ts
import { dialog } from '@owear/core'

const open = await dialog.showOpenDialog({
  title: 'Open files',
  properties: ['openFile', 'multiSelections'],
  filters: [{ name: 'Text', extensions: ['txt', 'md', 'json'] }],
})
if (!open.canceled) console.log(open.filePaths)

const save = await dialog.showSaveDialog({
  title: 'Save as',
  defaultPath: 'notes.md',
  filters: [{ name: 'Markdown', extensions: ['md'] }],
})
if (!save.canceled) console.log(save.filePath)

const box = await dialog.showMessageBox({
  type: 'question',
  title: 'Confirm',
  message: 'Continue?',
  detail: 'This will overwrite the file.',
  buttons: ['Yes', 'No', 'Cancel'],
  defaultId: 0,
  cancelId: 2,
  checkboxLabel: "Don't ask again",
})
console.log(box.response, box.checkboxChecked)
```

`showOpenDialog` options:

```ts
interface OpenDialogOptions {
  title?: string
  defaultPath?: string
  buttonLabel?: string
  filters?: { name: string; extensions: string[] }[]
  properties?: Array<
    'openFile' | 'openDirectory' | 'multiSelections' | 'showHiddenFiles' | 'createDirectory'
  >
}
```

`showMessageBox` options:

```ts
interface MessageBoxOptions {
  type?: 'none' | 'info' | 'error' | 'question' | 'warning'
  title?: string
  message: string
  detail?: string
  buttons?: string[]
  defaultId?: number
  cancelId?: number
  checkboxLabel?: string
}
```

### Raw module style

From the renderer without the SDK:

```ts
const path = await ow.invoke<string | null>('dialog', 'open', 'open', 'Open a file')
const files = await ow.invoke<string[] | null>('dialog', 'open', 'multi', 'Open many')
const resp = await ow.invoke<number>('dialog', 'messageBox', 'info', 'Title', 'Body', ['OK'])
```

`dialog.open(mode, title?, defaultPath?, filters?)` where `mode` ∈
`open | multi | save | dir`; returns `null` when cancelled.

### Platform notes

- Linux: `GtkFileChooserNative` / `GtkMessageDialog`. Dialogs run on the GTK main
  thread, so they are safe to invoke synchronously from the module.
- Windows: `IFileDialog`.
- Modal dialogs cannot be automated in CI; treat them as
  `VERIFY-ON-REAL-DESKTOP`.

## Notifications

```ts
// renderer
const id = await ow.invoke<number>('notification', 'show', 'My App', 'Job finished', 'My App')

// main process
await invokeNative('notification', 'show', 'Title', 'Body', 'App Name')
```

`notification.show(title, body?, appName?)` returns the system notification id.

- Linux: `org.freedesktop.Notifications` over D-Bus. If no notification daemon is
  running, it resolves with an error.
- Windows: toast (verify in CI).

## Next steps

- [`dialog` module reference](../api/modules/dialog.md) and
  [`notification`](../api/modules/notification.md).
- [`dialog` SDK reference](../api/sdk/dialog.md).
