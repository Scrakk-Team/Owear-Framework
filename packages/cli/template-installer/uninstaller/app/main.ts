// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// app/main.ts — proceso principal del DESINSTALADOR (sidecar Node).
import { app, BrowserWindow } from '@owear/core'

app.whenReady().then(() => {
  const win = new BrowserWindow({
    title: 'Desinstalador',
    width: 720,
    height: 460,
    titleBarStyle: 'custom',
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })
  win.on('closed', () => app.quit())
})
