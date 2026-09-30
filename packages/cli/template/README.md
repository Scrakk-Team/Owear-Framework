<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# __APP_NAME__

Desktop app built with **[Owear](https://owear.dev)**: the system WebView renders
your web frontend and the native kernel gives you the operating system.

## Get started

```bash
npm install
npm run dev     # ≡ ow dev
```

A window with the system WebView opens. Edit `src/renderer.ts` and Vite's
hot-reload reflects it instantly.

```bash
npm run build   # ≡ ow build → dist/
```

## Structure

```
├── index.html          UI (custom title bar, hero, cards)
├── src/
│   ├── renderer.ts     renderer logic + calls to native modules
│   ├── style.css       styles
│   └── ow.d.ts         types for window.ow
└── app/
    └── main.ts         main process (Node sidecar): just the window
```

## How it talks to the system

No `ipcRenderer` and no `ipcMain`: the renderer calls the kernel's native modules
directly.

```ts
const path = await ow.invoke('dialog', 'open', 'open', 'Open a file')
const text = await ow.invoke('fs', 'readText', path)
await ow.invoke('notification', 'show', 'Done', `Read ${text.length} bytes`)
```

Stock modules: `fs` · `path` · `process` (PTY) · `dialog` · `clipboard` ·
`shell` · `screen` · `net` · `notification` · `power` · `menu` ·
`globalshortcut` · `updater` · `capturer`, plus the builtins `ow-window`
(window), `window` (WebView extras), `session`, `app`, and `crashreporter`.

## Adding your own native module (C++)

1. Create `native/my_module.cpp`:

   ```cpp
   #include <ow/Json.h>
   #include <ow/Module.h>
   #include "ow_api.h"

   static void ping(const ow_request_t*, ow_response_t* res) {
       ow::Module::RespondOk(res, "\"pong\"");
   }

   OW_MODULE_BEGIN(my_module, "1.0.0")
   OW_FN(ping)
   OW_MODULE_END()
   ```

2. `ow dev` compiles it to `.owm` and generates the binding. Use it in the
   renderer:

   ```ts
   import { my_module } from '@owear/native'
   await my_module.ping()
   ```

> Requires a C++ compiler on the system. If you do not have one, do not add
> `native/` and the app keeps working with the stock modules.

## Learn more

The full documentation lives in the Owear repository under
[`docs/`](https://github.com/owear/owear/tree/main/docs).
