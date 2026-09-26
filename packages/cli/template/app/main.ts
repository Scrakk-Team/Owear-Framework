// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// app/main.ts — proceso principal (sidecar Node), estilo Electron.
//
// Sólo gestiona el ciclo de vida de la ventana. La app en sí vive en el
// renderer, que llama a los módulos nativos directo. Aquí podrías añadir
// lógica privilegiada (por ejemplo, exponerla vía un módulo nativo propio).

import { app, BrowserWindow } from '@owear/core'

app.whenReady().then(() => {
  const win = new BrowserWindow({
    title: '__APP_NAME__',
    width: 1080,
    height: 720,
    titleBarStyle: 'custom', // titlebar propia + regiones [data-ow-drag]
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })

  win.on('ready-to-show', () => console.log('[main] ventana lista, id =', win.id))
  win.on('closed', () => app.quit())
})
