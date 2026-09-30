---
title: window
description: Window extras coupled to the WebView, plus the internal ow-window builtin for
order: 25
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `window`

Window extras coupled to the WebView, plus the internal `ow-window` builtin for
title bars and lifecycle.

- **Kind:** builtin
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

This manifest declares two descriptors: `window` (extras) and `ow-window`
(title bar / lifecycle).

## `ow-window`

Available on all platforms. Functions that take a `windowId` inspect a specific
window; the drag/resize functions operate on the **invoking** window and take no
`windowId`.

| Function | Signature |
|---|---|
| `minimize` | `(windowId) → null` |
| `maximize` | `(windowId, enabled) → null` |
| `close` | `(windowId) → null` (vetoable) |
| `focus` | `(windowId) → null` |
| `setTitle` | `(windowId, title) → null` |
| `isMaximized` | `(windowId) → boolean` |
| `respondCloseRequest` | `(windowId, requestId, allow) → null` |
| `beginMoveDrag` | `() → null` (invoking window) |
| `beginResizeDrag` | `(edge) → { edge }` (invoking window) |

`beginResizeDrag` edges: `left`, `right`, `top`, `bottom`, `top-left`,
`top-right`, `bottom-left`, `bottom-right`. An unknown edge is rejected **without
starting the drag**, and the response echoes the effective edge so you can verify
parsing without depending on the window manager.

### Close veto

`close` (and the window close button) emits `closeRequested { requestId }` to the
renderer. The renderer answers:

```ts
ow.on('closeRequested', (p) => {
  ow.invoke('ow-window', 'respondCloseRequest', window.__owWindowId, p.requestId, false)
})
```

If nobody answers within `OW_CLOSE_TIMEOUT_MS` (default 1000 ms), the kernel
closes anyway. From the SDK, use `win.closeRespond(requestId, allow)`.

## `window` (extras)

| Function | Signature |
|---|---|
| `openDevTools` | `(windowId, show?) → null` |
| `capturePage` | `(windowId, { base64? }?) → { __ow_shm, format } \| { data, format }` |
| `setAlwaysOnTop` | `(windowId, on) → null` |
| `isAlwaysOnTop` | `(windowId) → boolean` |
| `setOpacity` | `(windowId, 0..1) → null` |
| `flashFrame` | `(windowId, on) → null` |
| `setIcon` | `(windowId, pngB64) → null` |
| `setUserAgent` | `(windowId, ua) → null` |
| `zoom` | `(windowId, factor) → null` |
| `reload` / `stop` | `(windowId) → null` |
| `goBack` / `goForward` | `(windowId) → null` |
| `canGoBack` / `canGoForward` | `(windowId) → boolean` |
| `getURL` / `getTitle` | `(windowId) → string` |
| `findInPage` | `(windowId, text, { matchCase?, backwards? }?) → { matches, active }` |
| `findStop` | `(windowId) → null` |
| `print` | `(windowId) → null` |

`capturePage` returns a PNG in shared memory by default; pass `{ base64: true }`
to get `{ data, format }` with base64 bytes instead (used by the SDK's
`webContents.capturePage`).

`findInPage` options: `matchCase` (default false) and `backwards` (default
false).

## Feature matrix

| Feature | Linux | Windows |
|---|---|---|
| `ow-window` lifecycle | ✓ | ✓ |
| devtools, capture, alwaysOnTop, opacity, flash, icon, UA, zoom | ✓ | ✓ |
| navigation + findInPage | ✓ | navigation only |
| `print` | ✓ | ✓ |

`printToPDF`, `progressBar`, `ignoreMouseEvents`, and `contentProtection` on the
window are either handled elsewhere (taskbar progress via `BrowserWindow`) or not
supported by WebKitGTK v1 and return a clear error.

## See also

- [Windows guide](../../guides/windows.md)
- [`BrowserWindow` SDK](../../api/sdk/browser-window.md) and [`webContents`](../../api/sdk/web-contents.md)
