---
title: Menu and MenuItem
description: Menus are built in the main process. Clicks run main-process handlers (and are
order: 8
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `Menu` and `MenuItem`

Menus are built in the main process. Clicks run main-process handlers (and are
also broadcast as `menu.click`).

```ts
import { Menu } from '@owear/core'

const menu = Menu.buildFromTemplate([...])
Menu.setApplicationMenu(menu)
menu.popup({ window: win })
```

## `MenuItemConstructorOptions`

```ts
interface MenuItemConstructorOptions {
  id?: string
  label?: string
  role?: MenuItemRole
  type?: 'normal' | 'separator' | 'submenu' | 'checkbox' | 'radio'
  checked?: boolean
  enabled?: boolean
  visible?: boolean
  accelerator?: string
  sublabel?: string
  toolTip?: string
  submenu?: Array<MenuItemConstructorOptions | MenuItem> | Menu
  click?: (menuItem: MenuItem, window: BrowserWindow | undefined) => void
}
```

`type` defaults to `'submenu'` when `submenu` is set, otherwise `'normal'`.

## Roles

```ts
type MenuItemRole =
  | 'undo' | 'redo' | 'cut' | 'copy' | 'paste' | 'pasteAndMatchStyle' | 'selectAll'
  | 'delete' | 'reload' | 'forceReload' | 'toggleDevTools'
  | 'resetZoom' | 'zoomIn' | 'zoomOut' | 'togglefullscreen'
  | 'minimize' | 'close' | 'quit' | 'about' | 'hide' | 'hideOthers' | 'unhide'
```

Roles are executed by the main process. `click` overrides `role`.

## Accelerators

Electron-style strings; `CmdOrCtrl` maps to `Ctrl`.

```ts
{ label: 'Save', accelerator: 'CmdOrCtrl+S', click: save }
```

## `Menu`

```ts
class Menu {
  items: MenuItem[]
  constructor(items?: MenuItem[])

  static buildFromTemplate(template, idPrefix?): Menu
  static setApplicationMenu(menu: Menu | null): void
  static getApplicationMenu(): Menu | null

  append(item: MenuItem): void
  popup(options?: { window?: BrowserWindow; x?: number; y?: number; items? }): void
}
```

> `setApplicationMenu` is a **no-op on Linux** (by design); use `popup` or draw a
> web menubar. It maps to a native `HMENU` on Windows.

## `MenuItem`

```ts
class MenuItem {
  id: string
  label: string
  role?: MenuItemRole
  type: 'normal' | 'separator' | 'submenu' | 'checkbox' | 'radio'
  checked: boolean
  enabled: boolean
  visible: boolean
  accelerator?: string
  sublabel?: string
  toolTip?: string
  submenu?: Menu
  click?: (menuItem: MenuItem, window?: BrowserWindow) => void
  constructor(opts?: MenuItemConstructorOptions, idHint?: string)
}
```

## Example

```ts
Menu.setApplicationMenu(
  Menu.buildFromTemplate([
    {
      label: 'File',
      submenu: [
        { label: 'New', accelerator: 'CmdOrCtrl+N', click: () => newDoc() },
        { type: 'separator' },
        { role: 'quit' },
      ],
    },
    {
      label: 'Edit',
      submenu: [{ role: 'undo' }, { role: 'redo' }, { type: 'separator' }, { role: 'copy' }, { role: 'paste' }],
    },
  ]),
)
```

Return value of `popup` is `void`; the menu lives until a selection is made.
