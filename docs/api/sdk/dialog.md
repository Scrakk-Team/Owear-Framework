---
title: dialog
description: Native file pickers and message boxes, backed by the dialog module.
order: 6
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `dialog`

Native file pickers and message boxes, backed by the `dialog` module.

```ts
import { dialog } from '@owear/core'
```

## `showOpenDialog`

```ts
dialog.showOpenDialog(options?: OpenDialogOptions)
  : Promise<{ canceled: boolean; filePaths: string[] }>
```

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

## `showSaveDialog`

```ts
dialog.showSaveDialog(options?: SaveDialogOptions)
  : Promise<{ canceled: boolean; filePath: string }>
```

```ts
interface SaveDialogOptions {
  title?: string
  defaultPath?: string
  buttonLabel?: string
  filters?: { name: string; extensions: string[] }[]
}
```

## `showMessageBox`

```ts
dialog.showMessageBox(options: MessageBoxOptions)
  : Promise<{ response: number; checkboxChecked: boolean }>
```

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

`response` is the index of the pressed button. `checkboxChecked` reflects the
optional checkbox.

## Example

```ts
const { canceled, filePaths } = await dialog.showOpenDialog({
  title: 'Open',
  properties: ['openFile', 'multiSelections'],
  filters: [{ name: 'Markdown', extensions: ['md'] }],
})

const { response } = await dialog.showMessageBox({
  type: 'question',
  message: 'Save changes?',
  buttons: ['Save', 'Discard', 'Cancel'],
  defaultId: 0,
  cancelId: 2,
})
```

## Notes

- Implemented by the native `dialog` module; on Linux these are GTK dialogs and
  cannot be automated in CI.
- `OpenDialogOptions.properties` maps to file/folder selection and hidden-file
  visibility.
