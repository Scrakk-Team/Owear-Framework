---
title: Quick start
description: This page gets a window on your screen with as little ceremony as possible.
order: 4
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Quick start

This page gets a window on your screen with as little ceremony as possible.

## 1. Create a project

```bash
ow create my-app
cd my-app
npm install
```

You get a working starter: Vite + TypeScript frontend, a Node main process, a
custom title bar, and a demo of most native APIs.

## 2. Run it

```bash
npm run dev
```

`ow dev` does four things:

1. starts the Vite dev server on `http://localhost:5173`;
2. compiles `native/*.cpp` (if any) into `.owm` modules;
3. compiles `app/main.ts` to JavaScript with esbuild;
4. launches the native kernel, pointing it at the dev server and the sidecar.

Close the window (or `Ctrl+C`) to stop everything.

## 3. Make it yours

The two files you will edit first:

- `src/renderer.ts` — the UI. It runs inside the WebView and calls native code
  through `window.ow`.

  ```ts
  const text = await window.ow.invoke<string>('fs', 'readText', '/etc/hostname')
  ```

- `app/main.ts` — the Node main process. It creates windows and can expose Node
  features to the UI.

  ```ts
  import { app, BrowserWindow } from '@owear/core'

  app.whenReady().then(() => {
    const win = new BrowserWindow({ title: 'My App', width: 900, height: 600 })
    win.loadURL(process.env.OW_DEV_SERVER_URL!)
  })
  ```

## 4. Build for production

```bash
ow build            # frontend + main.js + modules → dist/
ow build app        # a single self-contained binary → release/
```

See [Packaging](../guides/packaging.md) for `.deb`, AppImage, and MSI output.

## Next steps

- [Project structure](project-structure.md) — understand every folder.
- [Your first app](your-first-app.md) — build something real, step by step.
- [Windows](../guides/windows.md) and [Renderer API](../guides/renderer-api.md)
  — the two APIs you will use the most.
