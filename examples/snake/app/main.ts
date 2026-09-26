// app/main.ts — proceso principal MÍNIMO (sidecar Node).
//
// Tesis del ejemplo: en Owear el main es OPCIONAL. El juego vive entero en el
// renderer y llama a los módulos nativos DIRECTO (fs, path, notification,
// ow-window), así que este archivo no participa del gameplay ni del camino
// caliente. Sólo crea la ventana y cierra la app.
//
// En Electron este proceso sería imprescindible: TODA llamada a fs/dialog/net
// desde la UI tendría que venir aquí por `ipcMain.handle`, con su serialización
// de ida y vuelta. Aquí no hay ni un handler de IPC.

import { app, BrowserWindow } from '@owear/core'

app.whenReady().then(() => {
  const win = new BrowserWindow({
    title: 'Owear Snake',
    width: 860,
    height: 820,
    titleBarStyle: 'custom', // titlebar propio + regiones [data-ow-drag]
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })

  win.on('closed', () => app.quit())
})
