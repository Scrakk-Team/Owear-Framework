// app/main.ts — ventana mínima (el ejemplo vive en el renderer).
import { app, BrowserWindow } from '@owear/core'

app.whenReady().then(() => {
  const win = new BrowserWindow({
    title: 'Cursor X-Ray — Owear',
    width: 980,
    height: 660,
    titleBarStyle: 'custom',
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })

  win.on('closed', () => app.quit())
})
