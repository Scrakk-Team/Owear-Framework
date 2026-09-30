---
title: WebContents
description: win.webContents exposes the window's web content, mirroring Electron's naming.
order: 20
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `WebContents`

`win.webContents` exposes the window's web content, mirroring Electron's naming.
It extends `EventEmitter`.

```ts
const wc = win.webContents
wc.id            // window id (or -1 before creation)
```

## Methods

```ts
wc.send(name: string, payload?: unknown): Promise<void>
// Push an event only to this window. Arrives at `ow.on(name, cb)`.

wc.loadURL(url: string): Promise<void>
wc.reload(): Promise<void>
wc.openDevTools(): Promise<void>
wc.getURL(): Promise<string>
wc.getTitle(): Promise<string>

wc.executeJavaScript<T = unknown>(js: string): Promise<T>

wc.capturePage(): Promise<NativeImage>       // PNG, resizable (see nativeImage)
wc.printToPDF(options?): Promise<Buffer>     // platform dependent
wc.print(options?): void                     // system print dialog

wc.setWindowOpenHandler(handler | null): void
```

### `setWindowOpenHandler`

Controls `window.open` and `target="_blank"`:

```ts
wc.setWindowOpenHandler(({ url }) => {
  if (url.startsWith('https://trusted.example')) return { action: 'allow' }
  return { action: 'deny' }
})
```

Handler return: `{ action: 'allow' | 'deny', overrideBrowserWindowOptions? }`. The
default is `allow`. A handler that throws is treated as `allow`.

## Events

Electron-style (dash) names:

```ts
wc.on('did-finish-load', () => {})
wc.on('did-fail-load', (payload) => {})          // { url, code, description }
wc.on('did-start-navigation', (payload) => {})
wc.on('did-navigate', (payload) => {})           // committed navigation
wc.on('page-title-updated', (payload) => {})
wc.on('before-input-event', (e) => {})
```

These are also emitted on the `BrowserWindow` itself using the native names
(`didFinishLoad`, `navigationStarted`, `loadCommitted`, `pageTitleUpdated`,
`beforeInput`), plus the dash aliases (`page-title-updated`,
`enter-full-screen`, `leave-full-screen`, `always-on-top-changed`).

## Notes

- `capturePage` returns a `NativeImage`, so you can `.resize()`/`.crop()`/`.toPNG()`
  without touching the kernel again.
- On Linux (`WebKitGTK v2.52`), `printToPDF` is unavailable and returns an error;
  `print` opens the system dialog.
