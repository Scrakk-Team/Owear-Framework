---
title: Native modules
description: Every API in Owear lives in api/<name/ and is declared by a manifest
order: 1
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Native modules

Every API in Owear lives in `api/<name>/` and is declared by a manifest
(`owear.module.json`), which is the single source of truth. A module is either a
**`.owm` dynamic library** loaded at runtime or a **builtin** linked into the
kernel.

From your code, both are called the same way:

```ts
// renderer
const text = await ow.invoke<string>('fs', 'readText', '/etc/hostname')

// main process
import { invokeNative } from '@owear/core'
const text = await invokeNative<string>('fs', 'readText', '/etc/hostname')
```

## Modules in this reference

| Module | Kind | Summary |
|---|---|---|
| [`app`](app.md) | builtin | Single-instance lock, relaunch, badge |
| [`capturer`](capturer.md) | module | Screen capture and sources (X11) |
| [`clipboard`](clipboard.md) | module | Clipboard text and images |
| [`crashreporter`](crashreporter.md) | builtin | Signal handlers and backtraces |
| [`dialog`](dialog.md) | module | Native file/message dialogs |
| [`fs`](fs.md) | module | Filesystem, handles, watchers |
| [`globalshortcut`](globalshortcut.md) | module (optional) | Global keyboard shortcuts (X11) |
| [`installer`](installer.md) | builtin | Installer/uninstaller operations |
| [`menu`](menu.md) | module | Context menus |
| [`net`](net.md) | module | Native HTTP(S) without CORS |
| [`node`](node.md) | builtin | Renderer → main-process bridge |
| [`notification`](notification.md) | module | System notifications |
| [`path`](path.md) | module | Path math and standard directories |
| [`power`](power.md) | module | Battery, idle, suspend, inhibitors |
| [`process`](process.md) | module | Child processes and PTYs |
| [`safestorage`](safestorage.md) | module | OS-backed encryption |
| [`screen`](screen.md) | module | Monitors and cursor |
| [`session`](session.md) | builtin | Cookies, cache, proxy, downloads |
| [`shell`](shell.md) | module | Open URLs, paths, folders |
| [`theme`](theme.md) | module | Light/dark preference |
| [`tray`](tray.md) | module (optional) | System tray icon |
| [`updater`](updater.md) | module | Auto-update primitives |
| [`webview`](webview.md) | builtin | Embedded child webviews |
| [`window`](window.md) | builtin | Window extras and `ow-window` |

## How to read a module page

Each page documents the module's functions with their argument shapes, return
values, emitted events, and platform notes. Internally, arguments and results
cross the bridge as a JSON array; a function's first positional argument is
`args[0]`, and so on.
