---
title: Your first app
description: We will build Notes, a small app that 
order: 5
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Your first app

We will build **Notes**, a small app that:

- lists `.md` files from a folder you choose;
- shows their content;
- saves changes;
- has a native context menu and a system notification;
- remembers the chosen folder.

This tutorial exercises the renderer API, native modules, the main process, and
packaging, so you see how the pieces fit.

## 1. Scaffold

```bash
ow create notes
cd notes
pnpm install
pnpm dev
```

You should see the Owear starter window. We will replace its UI.

## 2. The renderer talks to the filesystem

Open `src/renderer.ts` and replace its body with the following. Everything runs
inside the WebView; `window.ow` is injected by the kernel.

```ts
// src/renderer.ts
import './style.css'

interface Entry {
  name: string
  type: 'file' | 'dir' | 'other'
}

let dir = ''

const $ = <T extends HTMLElement>(sel: string) => document.querySelector(sel) as T

/** Ask the OS for a folder, then list its markdown files. */
async function chooseFolder(): Promise<void> {
  const picked = await window.ow.invoke<{ canceled: boolean; filePaths: string[] }>(
    'dialog',
    'showOpenDialog',
    { properties: ['openDirectory'] },
  )
  if (picked.canceled || picked.filePaths.length === 0) return
  dir = picked.filePaths[0]
  await refresh()
}

async function refresh(): Promise<void> {
  if (!dir) return
  const entries = await window.ow.invoke<Entry[]>('fs', 'readDir', dir)
  const files = entries.filter((e) => e.type === 'file' && e.name.endsWith('.md'))

  const list = $('#list')
  list.innerHTML = ''
  for (const f of files) {
    const li = document.createElement('li')
    li.textContent = f.name
    li.addEventListener('click', () => void openFile(f.name))
    list.appendChild(li)
  }
}

async function openFile(name: string): Promise<void> {
  const path = `${dir}/${name}`
  const text = await window.ow.invoke<string>('fs', 'readText', path)
  const editor = $('#editor') as HTMLTextAreaElement
  editor.value = text
  editor.dataset.path = path
}

async function save(): Promise<void> {
  const editor = $('#editor') as HTMLTextAreaElement
  const path = editor.dataset.path
  if (!path) return
  await window.ow.invoke('fs', 'writeFile', path, editor.value)
  await window.ow.invoke('notification', 'show', 'Notes', `Saved ${path}`)
}

$('#choose').addEventListener('click', () => void chooseFolder())
$('#save').addEventListener('click', () => void save())
```

Because we call `fs` and `dialog` **directly from the renderer**, there is no
IPC hop through Node. The WebView talks to the kernel and the kernel loads the
`fs`/`dialog` `.owm` modules.

Add minimal markup to `index.html`:

```html
<button id="choose">Choose folder…</button>
<button id="save">Save</button>
<ul id="list"></ul>
<textarea id="editor" rows="20"></textarea>
```

Reload the dev window (the starter wires hot reload). Choose a folder with a
`.md` file, click it, edit, and save.

## 3. Remember the folder

Reopening the app forgets `dir`. Store it in the main process where Node APIs
are available, or simply in `localStorage` (it persists per app):

```ts
// at the end of renderer.ts
window.addEventListener('DOMContentLoaded', () => {
  dir = localStorage.getItem('notes.dir') ?? ''
  void refresh()
})
// and inside chooseFolder, after setting dir:
localStorage.setItem('notes.dir', dir)
```

## 4. Add a native context menu

Menus need the main process because they are built with `@owear/core`. In
`app/main.ts`, register a handler the renderer can call:

```ts
import { app, BrowserWindow, Menu } from '@owear/core'

app.whenReady().then(() => {
  const win = new BrowserWindow({
    title: 'Notes',
    width: 900,
    height: 640,
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })

  app.handle('notes.menu', async (name: string) => {
    Menu.buildFromTemplate([
      { label: `Rename ${name}`, click: () => void app.send('notes.action', 'rename') },
      { role: 'copy' },
      { type: 'separator' },
      { role: 'quit' },
    ]).popup({ window: win })
    return null
  })
})
```

From the renderer, right-click a file to open it:

```ts
li.addEventListener('contextmenu', (e) => {
  e.preventDefault()
  void window.ow.invoke('node', 'call', { fn: 'notes.menu', args: [f.name] })
})
```

`node/call` forwards to the handler registered with `app.handle` and resolves
with its return value. Main-to-renderer messages use `app.send` and arrive at
`ow.on`:

```ts
window.ow.on('notes.action', (action) => console.log('menu chose', action))
```

## 5. Save with a native shortcut

Menus support accelerators, and the SDK maps them to native shortcuts:

```ts
Menu.setApplicationMenu(
  Menu.buildFromTemplate([
    {
      label: 'File',
      submenu: [
        { label: 'Save', accelerator: 'CmdOrCtrl+S', click: () => void app.send('notes.action', 'save') },
        { role: 'quit' },
      ],
    },
  ]),
)
```

On Linux the application menubar is a no-op by design (GNOME does not use one);
the accelerator still works through the custom title bar / renderer. See
[Menus and tray](../guides/menus-and-tray.md).

## 6. Build and ship

```bash
pnpm build            # dist/: frontend + main.js + modules
ow build app          # a single binary in release/
```

To distribute with an installer and auto-update, continue with
[Installers](../guides/installers.md) and [Auto-update](../guides/auto-update.md).

## Where to go next

- [Renderer API](../guides/renderer-api.md) — the full `window.ow` surface.
- [Native modules](../guides/native-modules.md) — push hot paths into C++.
- [Filesystem](../guides/filesystem.md) — large files and watchers.
