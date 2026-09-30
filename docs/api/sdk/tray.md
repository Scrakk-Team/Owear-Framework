---
title: Tray
description: A Tray is a system tray icon, reusing the Menu type for its context menu.
order: 19
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `Tray`

A `Tray` is a system tray icon, reusing the `Menu` type for its context menu.
There is one tray per app; creating a new `Tray` replaces the current one.

```ts
import { Tray, Menu, nativeImage } from '@owear/core'

const tray = new Tray(nativeImage.createFromPath('icon.png'))
tray.setContextMenu(Menu.buildFromTemplate([{ label: 'Show', click: () => win.show() }]))
tray.on('click', () => win.show())
```

## Constructor

```ts
new Tray(image?: NativeImage | string)
```

`image` may be a `NativeImage` or a path. It is applied after `app.whenReady()`.

## Methods

```ts
tray.setImage(image: NativeImage | string): void
tray.setPressedImage(image: NativeImage | string): void
tray.setToolTip(text: string): void
tray.setTitle(text: string): void
tray.setContextMenu(menu: Menu | null): void
tray.popupContextMenu(menu?: Menu): void
tray.destroy(): void
tray.id: string
```

Images are converted to PNG base64 internally before being sent to the kernel.

## Events

```ts
interface TrayEvent { button: 'left' | 'right' | 'double'; windowId: number }

tray.on('click', (e: TrayEvent) => {})
tray.on('right-click', (e: TrayEvent) => {})
tray.on('double-click', (e: TrayEvent) => {})
```

The tray events are routed from the kernel's `tray.event` messages.

## Platform notes

- Linux uses a hand-rolled `org.kde.StatusNotifierItem` + dbusmenu implementation
  over GDBus. It works on GNOME (with the appindicator extension), KDE, and XFCE
  (SNI plugin). It requires a StatusNotifierWatcher on the session bus.
- `setPressedImage` is a no-op on Linux.
- Windows (`Shell_NotifyIcon`) is the intended backend; verify in CI.
