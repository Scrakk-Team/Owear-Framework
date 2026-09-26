// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// app/main.ts — proceso principal (sidecar Node), estilo Electron.
//
// Sólo gestiona el ciclo de vida de la ventana. La app vive en el renderer,
// que llama a los módulos nativos directo.

import { app, BrowserWindow } from '@owear/core'

app.whenReady().then(() => {
  const win = new BrowserWindow({
    title: 'Owear Starter',
    width: 1080,
    height: 720,
    titleBarStyle: 'custom', // titlebar propia + regiones [data-ow-drag]
    // Botones de ventana nativos (min/max/close) dentro de la titlebar custom.
    // En Linux son los del TEMA (la distro define forma/tamaño/hover).
    // Opcional: color = fondo de la banda · symbolColor = glifo ·
    // buttonColor = fondo interno del círculo (si no, los del tema).
    titleBarOverlay: { height: 40 },
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })

  win.on('ready-to-show', () => console.log('[main] ventana lista, id =', win.id))
  win.on('closed', () => app.quit())
})
