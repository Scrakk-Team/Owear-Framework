---
title: BrowserWindow
description: A BrowserWindow is the main-process handle for a native window. It extends
order: 5
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `BrowserWindow`

A `BrowserWindow` is the main-process handle for a native window. It extends
`EventEmitter`.

```ts
import { app, BrowserWindow } from '@owear/core'

app.whenReady().then(() => {
  const win = new BrowserWindow({ title: 'App', width: 900, height: 600 })
})
```

> Must be constructed after `app.whenReady()` resolves; otherwise it throws
> `BrowserWindow created before app.whenReady()`. Creation is asynchronous: the
> window id is available after `ready-to-show`.

## Constructor options

```ts
interface WindowOptions {
  title?: string
  width?: number; height?: number
  x?: number; y?: number
  minWidth?: number; minHeight?: number
  maxWidth?: number; maxHeight?: number
  resizable?: boolean
  movable?: boolean
  minimizable?: boolean
  maximizable?: boolean
  closable?: boolean
  fullscreenable?: boolean
  frameless?: boolean
  transparent?: boolean
  backgroundColor?: string
  show?: boolean
  skipTaskbar?: boolean
  alwaysOnTop?: boolean
  hasShadow?: boolean
  aspectRatio?: number
  parent?: number
  modal?: boolean
  titleBarStyle?: 'default' | 'hidden' | 'custom'
  titleBarOverlay?: boolean | {
    enabled?: boolean; color?: string; symbolColor?: string
    buttonColor?: string; height?: number
  }
  url?: string
  session?: string
  icon?: string
}
```

See the [Windows guide](../../guides/windows.md) for descriptions.

## Properties

```ts
win.id: number | null            // null until created
win.webContents: WebContents
```

## Loading

```ts
win.loadURL(url: string): Promise<void>
win.eval<T = unknown>(js: string): Promise<T>          // JSON-serialized result
```

## Lifecycle

```ts
win.show(): Promise<void>
win.hide(): Promise<void>
win.focus(): Promise<void>
win.close(): Promise<void>          // vetoable
win.destroy(): Promise<void>        // immediate
win.minimize(): Promise<void>
win.maximize(): Promise<void>
win.unmaximize(): Promise<void>
win.setFullScreen(enabled: boolean): Promise<void>
win.closeRespond(requestId: number, allow: boolean): Promise<void>
```

## State (getters)

```ts
win.isMaximized() / isMinimized() / isFullScreen() / isVisible() / isFocused()
win.isResizable() / isMovable() / isMinimizable() / isMaximizable() / isClosable()
win.isAlwaysOnTop() / isKiosk() / isDestroyed()
win.getBounds(): Promise<{ x, y, width, height }>
win.setBounds(partial): Promise<void>
win.getContentBounds() / getContentSize() / getMinimumSize() / getMaximumSize()
```

## Mutators

```ts
win.setTitle(title): Promise<void>
win.setTitleBarOverlay(overlay): Promise<void>
win.setIcon(path): Promise<void>
win.setResizable(on?) / setMovable(on?) / setMinimizable(on?) / setMaximizable(on?)
win.setClosable(on?)
win.setAlwaysOnTop(on?, level?) / setSkipTaskbar(on?) / setHasShadow(on?)
win.setKiosk(on?)
win.setIgnoreMouseEvents(ignore?, { forward? })
win.setProgressBar(value, { mode?: 'none' | 'normal' | 'indeterminate' | 'paused' | 'error' })
win.setBackgroundColor(color)
win.moveTop()
win.setAspectRatio(ratio, extraSize?)
win.setContentSize(width, height)
win.setMinimumSize(width, height) / setMaximumSize(width, height)
```

## Events

```ts
win.on('ready-to-show', (id: number) => {})
win.on('closed', () => {})
win.on('resize', (bounds: Bounds) => {})
win.on('move', (bounds: Bounds) => {})
win.on('focus', () => {}) / win.on('blur', () => {})
win.on('maximize', () => {}) / win.on('unmaximize', () => {})
win.on('enterFullScreen', () => {}) / win.on('leaveFullScreen', () => {})
win.on('closeRequested', ({ requestId }: { requestId: number }) => {})
win.on('page-title-updated', (payload) => {})
win.on('always-on-top-changed', (payload) => {})
win.on('disconnected', () => {})
win.on('error', (err: Error) => {})
```

## Statics

```ts
BrowserWindow.getAllWindows(): BrowserWindow[]
BrowserWindow.getFocusedWindow(): BrowserWindow | null
BrowserWindow.fromId(id: number): BrowserWindow | null
```

## Notes

- `close()` triggers `closeRequested`, which is vetoable. If unanswered within
  `OW_CLOSE_TIMEOUT_MS` (default 1000 ms), the window closes anyway.
- `setProgressBar(-1)` with `mode: 'none'` clears the taskbar progress.
- `titleBarStyle: 'custom'` plus `titleBarOverlay` yields native buttons inside a
  custom title bar where the OS supports it; otherwise draw your own controls and
  call the `ow-window` module.
