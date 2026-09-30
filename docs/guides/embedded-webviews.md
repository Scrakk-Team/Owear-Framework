---
title: Embedded webviews
description: The webview builtin lets you create and control child webviews inside a
order: 5
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Embedded webviews

The `webview` builtin lets you create and control **child webviews inside a
window**. Each child is an independent WebView with its own process, embedded as
a native child. This is how you build browser-like UIs (tabs, previews, web
panels). Linux only, for now.

```ts
const { id } = await ow.invoke<{ id: number }>('webview', 'create', {
  url: 'https://owear.dev',
  x: 0, y: 0, width: 800, height: 600,
})
```

## Creating

```ts
webview.create(options): Promise<{ id: number }>
```

`options`: `{ url?, x?, y?, width?, height?, transparent?, userAgent? }`.

The child is positioned in window-relative coordinates and composited by the
native backend.

## Controlling

All commands take the webview id first:

```ts
webview.destroy(id)
webview.setBounds(id, { x, y, width, height })
webview.setVisible(id, visible)
webview.setZoom(id, factor)
webview.load(id, url)
webview.back(id) / forward(id) / reload(id) / stop(id)
webview.canBack(id) / canForward(id)
webview.getURL(id) / getTitle(id)
webview.eval(id, js)
webview.devtools(id, show?)
webview.findInPage(id, text, { matchCase?, backwards? })
webview.findStop(id)
```

`setBounds` is how you keep the native child aligned with a DOM element as the
page scrolls or resizes:

```ts
function syncBounds(id: number): void {
  const slot = document.getElementById('preview')!
  const r = slot.getBoundingClientRect()
  const visible = r.bottom > 0 && r.top < window.innerHeight && r.width > 8 && r.height > 8
  void ow.invoke('webview', 'setBounds', id, {
    x: Math.round(r.left), y: Math.round(r.top),
    width: Math.round(r.width), height: Math.round(r.height),
  })
  void ow.invoke('webview', 'setVisible', id, visible)
}

window.addEventListener('resize', () => syncBounds(id))
document.querySelector('.page')!.addEventListener('scroll', () => syncBounds(id), { passive: true })
```

## Events

```ts
ow.on('webview.loadChanged', ({ id, state, url }) => {})   // state: start | committed | finished
ow.on('webview.urlChanged', ({ id, url }) => {})
ow.on('webview.titleChanged', ({ id, title }) => {})
ow.on('webview.loadFailed', ({ id, url, message }) => {})
```

## Important notes

- The API is **window-scoped**: `create` embeds the child in the window that
  invoked it; commands must come from the same window.
- Each webview has its own process, so many tabs mean many processes — design
  for it.
- On platforms without support, calls resolve with
  `webviews not supported on this platform`.

## Next steps

- [`webview` module reference](../api/modules/webview.md).
- [Windows](windows.md) — the host window's options.
