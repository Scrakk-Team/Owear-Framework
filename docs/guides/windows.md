---
title: Windows (BrowserWindow)
description: BrowserWindow is the main-process handle for a native window. Its API mirrors
order: 24
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Windows (`BrowserWindow`)

`BrowserWindow` is the main-process handle for a native window. Its API mirrors
Electron so that porting is mostly mechanical, but creation is asynchronous
because the window is created by the native kernel over the control socket.

```ts
import { app, BrowserWindow } from '@owear/core'

app.whenReady().then(() => {
  const win = new BrowserWindow({
    title: 'My App',
    width: 1024,
    height: 700,
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })
})
```

> `BrowserWindow` must be constructed **after** `app.whenReady()` has resolved.
> Constructing it earlier throws `BrowserWindow created before app.whenReady()`.

## Options

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
  parent?: number                  // id of another window (owner/transient)
  modal?: boolean
  titleBarStyle?: 'default' | 'hidden' | 'custom'
  titleBarOverlay?: boolean | {
    enabled?: boolean; color?: string; symbolColor?: string
    buttonColor?: string; height?: number
  }
  url?: string                     // initial URL (http(s)://, app://, file://)
  session?: string                 // session partition, e.g. 'persist:account-2'
  icon?: string                    // path to a PNG/JPEG for this window
}
```

| Option | Notes |
|---|---|
| `titleBarStyle` | `default` keeps OS decorations; `hidden` = frameless; `custom` = your HTML title bar plus native window buttons where the OS supports them |
| `titleBarOverlay` | Enables and styles the native min/max/close buttons inside a custom title bar |
| `parent` / `modal` | Makes a transient/modal child window |
| `session` | Isolates cookies/storage/cache per profile. Create with `session.fromPartition(name)` |
| `icon` | Per-window icon. For a global default use `app.setIcon(path)` |

## Creating a window: async by design

```ts
new BrowserWindow(...)      // schedules creation
win.on('ready-to-show', () => console.log('id =', win.id))
```

`win.id` is `null` until the kernel confirms creation. Anything that needs the
window id must wait for `ready-to-show` or check `win.id`.

## Methods

### Loading and evaluating

```ts
win.loadURL(url: string): Promise<void>
win.eval<T>(js: string): Promise<T>          // result is JSON-serialized
win.webContents.executeJavaScript<T>(js: string): Promise<T>
```

### Lifecycle

```ts
win.show() / win.hide() / win.focus()
win.close()                 // vetoable: emits 'closeRequested'
win.destroy()               // immediate, no veto
win.minimize()
win.maximize() / win.unmaximize()
win.setFullScreen(enabled: boolean)
win.setKiosk(true) / win.isKiosk()
```

### State

```ts
win.isMaximized() / isMinimized() / isFullScreen() / isVisible() / isFocused()
win.isResizable() / isMovable() / isMinimizable() / isMaximizable() / isClosable()
win.isAlwaysOnTop() / isDestroyed()
win.getBounds() / setBounds(partial)
win.getContentBounds() / getContentSize() / setContentSize(w, h)
win.getMinimumSize() / getMaximumSize() / setMinimumSize(w, h) / setMaximumSize(w, h)
```

### Appearance and behavior

```ts
win.setTitle(title)
win.setTitleBarOverlay(overlay)     // re-configure native buttons on the fly
win.setResizable(on) / setMovable(on) / setMinimizable(on) / setMaximizable(on) / setClosable(on)
win.setAlwaysOnTop(on, level?) / setSkipTaskbar(on) / setHasShadow(on)
win.setIgnoreMouseEvents(ignore, { forward })
win.setProgressBar(value, { mode })   // -1 = indeterminate; taskbar progress
win.setBackgroundColor(color)
win.moveTop()
win.setAspectRatio(ratio, extraSize?)
win.setIcon(path)                      // per-window PNG/JPEG
```

## Events

```ts
win.on('ready-to-show', () => {})
win.on('closed', () => {})
win.on('resize', (bounds) => {})   // { x, y, width, height }
win.on('move', (bounds) => {})
win.on('focus', () => {}) / win.on('blur', () => {})
win.on('maximize', () => {}) / win.on('unmaximize', () => {})
win.on('enterFullScreen', () => {}) / win.on('leaveFullScreen', () => {})
win.on('closeRequested', ({ requestId }) => {})
win.on('disconnected', () => {})   // control socket lost
win.on('error', (err) => {})
```

## Vetoing close

`close()` (and the window's close button) trigger `closeRequested`. Answer with
`closeRespond(requestId, allow)`:

```ts
win.on('closeRequested', ({ requestId }) => {
  if (hasUnsavedChanges) {
    // show a dialog; then decide
    win.closeRespond(requestId, false)     // cancel
  } else {
    win.closeRespond(requestId, true)      // allow
  }
})
```

If nobody answers within `OW_CLOSE_TIMEOUT_MS` (default 1000 ms), the kernel
closes the window anyway.

## Static helpers

```ts
BrowserWindow.getAllWindows(): BrowserWindow[]
BrowserWindow.getFocusedWindow(): BrowserWindow | null
BrowserWindow.fromId(id): BrowserWindow | null
```

All three are synchronous, like Electron.

## Custom title bars

`titleBarStyle: 'custom'` gives you a frameless window and, on platforms that
allow it, native window buttons overlaid on your HTML. In your markup, mark the
draggable region and any controls inside it:

```html
<header data-ow-drag>
  <span>My App</span>
  <button data-ow-no-drag>Settings</button>
</header>
```

Recognized attributes:

| Attribute | Effect |
|---|---|
| `data-ow-drag` | Drag region (move the window) |
| `data-ow-no-drag` | Excluded from the drag region (clickable) |
| `data-ow-resize="bottom-right"` | Manual resize handle for frameless windows |

Edges for `data-ow-resize`: `left`, `right`, `top`, `bottom`, `top-left`,
`top-right`, `bottom-left`, `bottom-right`.

When the OS provides overlay buttons, the kernel sets `window.__owTitlebarOverlay`
so your CSS can reserve space for them:

```ts
if (window.__owTitlebarOverlay?.enabled) {
  document.documentElement.style.setProperty(
    '--titlebar-height',
    `${window.__owTitlebarOverlay.height}px`,
  )
}
```

To drive your own web buttons (when there is no native overlay):

```ts
const id = window.__owWindowId
await window.ow.invoke('ow-window', 'minimize', id)
await window.ow.invoke('ow-window', 'maximize', id, true)
await window.ow.invoke('ow-window', 'close', id)
```

See [`ow-window`](../api/modules/window.md) for drag/resize and the veto protocol.

## Next steps

- [Renderer API](renderer-api.md) — what the UI can call.
- [`BrowserWindow` reference](../api/sdk/browser-window.md).
- [`webContents` reference](../api/sdk/web-contents.md).
