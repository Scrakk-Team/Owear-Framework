---
title: Menus and tray
description: Menus are built in the main process with @owear/core and dispatched back to
order: 11
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Menus and tray

Menus are built in the main process with `@owear/core` and dispatched back to
the main process. Tray icons reuse the same `Menu` template.

## Building a menu

```ts
import { Menu } from '@owear/core'

const menu = Menu.buildFromTemplate([
  {
    label: 'File',
    submenu: [
      { label: 'New', accelerator: 'CmdOrCtrl+N', click: () => createNote() },
      { type: 'separator' },
      { role: 'quit' },
    ],
  },
  {
    label: 'View',
    submenu: [
      { label: 'Sidebar', type: 'checkbox', checked: true, click: (mi) => toggle(mi.checked) },
      { type: 'separator' },
      { role: 'toggleDevTools' },
      { role: 'reload' },
    ],
  },
])
```

### Item options

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

### Roles

`undo redo cut copy paste pasteAndMatchStyle selectAll delete reload forceReload
toggleDevTools resetZoom zoomIn zoomOut togglefullscreen minimize close quit
about hide hideOthers unhide`

Roles are handled in the main process (e.g. `quit` calls `app.quit()`, `copy`
runs `document.execCommand('copy')` in the focused window). If you set `click`,
it takes precedence over `role`.

### Accelerators

`accelerator` accepts Electron-style strings. `CmdOrCtrl` maps to `Ctrl` on
Linux and Windows.

```ts
{ label: 'Save', accelerator: 'CmdOrCtrl+S', click: save }
```

## Application menu

```ts
Menu.setApplicationMenu(menu)   // menubar
Menu.setApplicationMenu(null)   // remove
Menu.getApplicationMenu()
```

> **Linux:** the application menubar is a **no-op by design**. GNOME does not use
> a global menubar, so apps draw their own in web code. Accelerators still work.

## Context menu (popup)

```ts
app.handle('show.context', async () => {
  Menu.buildFromTemplate([
    { label: 'Rename', click: () => rename() },
    { type: 'separator' },
    { role: 'copy' },
  ]).popup({ window: win })
  return null
})
```

`popup({ window?, x?, y?, items? })` can also take `items` directly, so you do
not have to build a `Menu` first:

```ts
menu.popup({ window: win, items: [{ label: 'Copy', role: 'copy' }] })
```

On Linux the popup is positioned at the current pointer because the click
arrives asynchronously from the renderer (there is no native trigger event).

## Click routing

Clicks from a popup are delivered to the main process (the handler runs) and also
broadcast as `menu.click`. Popup items created from a template are registered by
id, so the main-process dispatcher can find the right `MenuItem`.

## Tray

```ts
import { Tray, Menu, nativeImage } from '@owear/core'

const tray = new Tray(nativeImage.createFromPath('icon.png'))
tray.setToolTip('My App')
tray.setTitle('My App')
tray.setContextMenu(Menu.buildFromTemplate([
  { label: 'Show window', click: () => win.show() },
  { type: 'separator' },
  { role: 'quit' },
]))

tray.on('click', () => win.show())
tray.on('right-click', () => console.log('right'))
tray.on('double-click', () => console.log('double'))
```

### Tray API

```ts
new Tray(image?: NativeImage | string)
tray.setImage(image)
tray.setPressedImage(image)
tray.setToolTip(text)
tray.setTitle(text)
tray.setContextMenu(menu | null)
tray.popupContextMenu(menu?)
tray.destroy()
tray.id
```

There is **one tray per app**; creating a second `Tray` replaces the current one.

### Platform notes

- **Linux:** implemented with the `org.kde.StatusNotifierItem` + dbusmenu
  standard, hand-rolled over GDBus (no libappindicator). It works on GNOME (with
  the appindicator extension), KDE, and XFCE with the SNI plugin. If no
  StatusNotifierWatcher is present, tray creation fails with a clear error.
- **Windows:** `Shell_NotifyIcon` (verify in CI).

The `tray` module is marked optional: if its system dependencies are missing it
is simply omitted from the build.

## Next steps

- [`menu` module reference](../api/modules/menu.md) and [`tray`](../api/modules/tray.md).
- [`Menu` SDK reference](../api/sdk/menu.md) and [`Tray`](../api/sdk/tray.md).
