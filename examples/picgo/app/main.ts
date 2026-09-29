// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// app/main.ts — proceso principal del port de PicGo a Owear (sustituye al
// `src/background.ts` + `src/main/*` de Electron).
//
// El renderer real de PicGo habla por `window.bridgeApi`; nuestro shim
// (`src/renderer/owear/bridge.ts`) lo reenvía aquí con
// `ow.invoke('node','call',{ fn: <canal>, args })`. Registramos los canales con
// `app.handle(<canal>, handler)`.
//
// Estado del port: aquí se van portando los canales del `ipcList` de PicGo.
// Los canales aún no portados devuelven null desde el shim (la UI arranca en
// modo degradado).

import { app, BrowserWindow } from '@owear/core'

app.whenReady().then(() => {
  // Nota: los handlers de canales (ipcList de PicGo) se registran aquí con
  // `app.handle('<canal>', async (args) => { ... })`.

  const win = new BrowserWindow({
    title: 'PicGo',
    width: 1100,
    height: 720,
    minWidth: 720,
    minHeight: 520,
    titleBarStyle: 'custom',
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })

  win.on('closed', () => app.quit())
})
