---
title: webview
description: Embedded child webviews inside a window.
order: 24
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `webview`

Embedded child webviews inside a window.

- **Kind:** builtin
- **Version:** 0.1.0
- **Platforms:** Linux

Each child webview is an independent WebView with its own process, embedded as a
native child of the invoking window. Commands must come from the same window.

## Functions

| Function | Signature |
|---|---|
| `create` | `({ url?, x?, y?, width?, height?, transparent?, userAgent? }) → { id }` |
| `destroy` | `(id) → null` |
| `setBounds` | `(id, { x, y, width, height }) → null` |
| `load` | `(id, url) → null` |
| `back` / `forward` / `reload` / `stop` | `(id) → null` |
| `canBack` / `canForward` | `(id) → boolean` |
| `getURL` / `getTitle` | `(id) → string` |
| `eval` | `(id, js) → result` |
| `setVisible` | `(id, visible) → null` |
| `setZoom` | `(id, factor) → null` |
| `devtools` | `(id, show?) → null` |
| `findInPage` | `(id, text, { matchCase?, backwards? }?) → { matches, active }` |
| `findStop` | `(id) → null` |

## Events

| Event | Payload |
|---|---|
| `webview.loadChanged` | `{ id, state, url }` (`state`: start/committed/finished) |
| `webview.urlChanged` | `{ id, url }` |
| `webview.titleChanged` | `{ id, title }` |
| `webview.loadFailed` | `{ id, url, message }` |

## Example

```ts
const { id } = await ow.invoke<{ id: number }>('webview', 'create', {
  url: 'https://owear.dev', x: 0, y: 0, width: 800, height: 600,
})
await ow.invoke('webview', 'setBounds', id, { x: 8, y: 48, width: 640, height: 400 })
await ow.invoke('webview', 'load', id, 'https://example.com')
ow.on('webview.urlChanged', ({ id: wv, url }) => { if (wv === id) show(url) })
```

## Notes

- On unsupported platforms, calls return `webviews not supported on this platform`.
- Each webview has its own process; budget for it.

## See also

- [Embedded webviews guide](../../guides/embedded-webviews.md)
