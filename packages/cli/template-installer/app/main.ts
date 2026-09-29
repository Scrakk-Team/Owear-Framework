// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// app/main.ts — proceso principal del INSTALADOR (sidecar Node).
//
// El instalador es una app Owear normal: aquí creas su ventana y, si quieres,
// lógica privilegiada. La UI (renderer) usa la API nativa `installer`.

import { app, BrowserWindow } from '@owear/core'

app.whenReady().then(() => {
  const win = new BrowserWindow({
    title: 'Instalador',
    width: 820,
    height: 640,
    resizable: true,
    titleBarStyle: 'custom', // titlebar propia + regiones [data-ow-drag]
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })

  win.on('closed', () => app.quit())
})
