---
title: Control protocol
description: The kernel exposes a control socket that the Node SDK (and any custom
order: 2
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Control protocol

The kernel exposes a **control socket** that the Node SDK (and any custom
client) uses to drive the app. The transport is NDJSON (one JSON object per
line).

## Transport

| Platform | Socket |
|---|---|
| Linux | `$XDG_RUNTIME_DIR/owear-<pid>.sock` |
| Windows | `\\.\pipe\owear-<pid>` |

Frames:

```jsonc
{"id":N,"cmd":"…","params":{…}}          // request
{"id":N,"ok":true,"result":…}            // response
{"id":N,"ok":false,"error":"…"}          // error
{"event":"window.event","params":{…}}    // server → client event
```

The SDK's `ControlChannel.call(cmd, params)` writes a request and resolves on the
matching response.

## Commands

### App

| Command | Params | Result |
|---|---|---|
| `app.info` | — | `{ pid, version, socket }` |
| `app.quit` | `{ exitCode? }` | — |
| `app.setName` | `{ name }` | — |
| `app.commandLine.appendSwitch` | `{ key, value? }` | — |
| `app.commandLine.appendArgument` | `{ arg }` | — |
| `node.ensure` | `{ range }` | `{ path, version, source }` |

### Modules

| Command | Params | Result |
|---|---|---|
| `module.list` | — | `[{ name, version, origin, builtin, functions, functionNames }]` |
| `module.info` | `{ name }` | one module object, or error |
| `module.invoke` | `{ module, method, args?, windowId? }` | module result |

### Windows

| Command | Params |
|---|---|
| `window.create` | full `WindowOptions` |
| `window.close` / `destroy` / `show` / `hide` / `focus` / `minimize` / `unmaximize` | `{ windowId }` |
| `window.maximize` | `{ windowId, enabled }` |
| `window.setFullScreen` | `{ windowId, enabled }` |
| `window.isMaximized` / `isMinimized` / `isVisible` / `isFocused` / `isResizable` / `isMovable` / `isMinimizable` / `isMaximizable` / `isClosable` / `isAlwaysOnTop` / `isKiosk` / `isDestroyed` / `isFullScreen` | `{ windowId }` |
| `window.getBounds` / `getContentBounds` / `getContentSize` / `getMinimumSize` / `getMaximumSize` | `{ windowId }` |
| `window.setBounds` | `{ windowId, x?, y?, width?, height? }` |
| `window.setContentSize` / `setMinimumSize` / `setMaximumSize` | `{ windowId, width, height }` |
| `window.setTitle` | `{ windowId, title }` |
| `window.loadURL` | `{ windowId, url }` |
| `window.eval` | `{ windowId, js }` |
| `window.capturePage` | `{ windowId, base64? }` |
| `window.printToPDF` | `{ windowId }` |
| `window.setTitleBarOverlay` | `{ windowId, titleBarOverlay }` |
| `window.setColorScheme` | `{ scheme }` (0 auto, 1 light, 2 dark) |
| `window.setAlwaysOnTop` / `setSkipTaskbar` / `setHasShadow` / `setKiosk` / `setIgnoreMouseEvents` / `setProgressBar` / `setBackgroundColor` / `setAspectRatio` / `moveTop` | `{ windowId, … }` |
| `window.respondCloseRequest` | `{ windowId, requestId, allow }` |
| `window.list` / `window.getFocused` | — |

### Sessions, menus, protocols, requests

| Command | Params |
|---|---|
| `session.setPermissionHandler` | `{ enabled }` |
| `session.respondPermission` | `{ id, allow }` |
| `menu.setApplicationMenu` | `{ items }` |
| `protocol.register` | `{ scheme, privileged?, dir?, handler? }` |
| `protocol.respond` | `{ reqId, status, headers, body }` |
| `webRequest.register` / `webRequest.unregister` | `{ urls }` |
| `webRequest.respond` | `{ id, cancel, redirectURL? }` |
| `webContents.setWindowOpenHandler` | `{ windowId, enabled }` |
| `webContents.respondWindowOpen` | `{ id, action }` |

### Node bridge

| Command | Params |
|---|---|
| `node.request` | `{ reqId, fn, args, windowId }` (kernel → main) |
| `node.respond` | `{ reqId, ok, result }` (main → kernel) |
| `node.emit` | `{ name, payload?, windowId? }` |

## Events (server → client)

```jsonc
{"event":"window.event","params":{"windowId":1,"name":"resize","payload":{…}}}
{"event":"app.event","params":{"name":"…","payload":…}}
{"event":"sdk.event","params":{…}}
{"event":"node.event","params":{…}}
{"event":"menu.click","params":{"id":"…","role":"…","windowId":1}}
{"event":"module.event","params":{"name":"…","payload":…}}
```

Window event names: `resize move focus blur maximize unmaximize enterFullScreen
leaveFullScreen closeRequested closed pageTitleUpdated didFinishLoad didFailLoad
navigationStarted loadCommitted beforeInput`.

## Example session

```jsonc
→ {"id":1,"cmd":"window.create","params":{"title":"Hi","url":"http://localhost:5173"}}
← {"id":1,"ok":true,"result":{"windowId":1}}
→ {"id":2,"cmd":"module.invoke","params":{"module":"fs","method":"readText","args":["/etc/hostname"]}}
← {"id":2,"ok":true,"result":"myhost\n"}
← {"event":"window.event","params":{"windowId":1,"name":"focus"}}
```
