// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// app/main.ts — proceso principal (sidecar Node), estilo Electron.
//
// Sólo gestiona el ciclo de vida de la ventana. La app vive en el renderer,
// que llama a los módulos nativos directo.

import { app, BrowserWindow, Menu, Tray, invokeNative, nativeImage, theme, screen, powerMonitor, powerSaveBlocker } from '@owear/core'
import * as fs from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'

app.whenReady().then(() => {
  // Icono de la app: `favicon.svg` del proyecto (Vite lo copia a dist/ en build).
  // `app.setIcon` fija el icono por defecto de las ventanas creadas después.
  // (API de icono del kernel: `window.setIcon`.)
  const iconPath = [
    path.join(app.getAppPath(), 'favicon.svg'), // empaquetado: dist/ → app/
    path.join(process.cwd(), 'public', 'favicon.svg'), // dev: public/ del proyecto
  ].find((p) => fs.existsSync(p))
  if (iconPath) app.setIcon(iconPath)

  const win = new BrowserWindow({
    title: '__APP_NAME__',
    width: 1080,
    height: 720,
    titleBarStyle: 'custom', // titlebar propia + regiones [data-ow-drag]
    // Botones de ventana nativos (min/max/close) dentro de la titlebar custom.
    // En Linux son los del TEMA (la distro define forma/tamaño/hover).
    // Opcional: color = fondo de la banda · symbolColor = glifo ·
    // buttonColor = fondo interno del círculo (si no, los del tema).
    titleBarOverlay: { height: 40, color: '#21252b' },
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })

  win.on('ready-to-show', () => console.log('[main] ventana lista, id =', win.id))
  win.on('closed', () => app.quit())

  // ── C3: webContents (capturePage, eventos, setWindowOpenHandler) ─────────
  app.handle('starter.capture', async () => {
    const img = await win.webContents.capturePage()
    return { data: img.toPNG().toString('base64') }
  })
  win.webContents.on('did-finish-load', () =>
    void app.send('webContents.event', 'did-finish-load')
  )
  win.webContents.on('before-input-event', (e: { key?: string }) =>
    void app.send('webContents.event', `before-input-event ${e?.key ?? ''}`)
  )
  win.webContents.setWindowOpenHandler(({ url }) => {
    void app.send('webContents.event', `window-open ${url}`)
    return { action: 'deny' }
  })

  // ── C4: nativeImage (captura → resize/crop/toPNG/dataURL) ────────────────
  app.handle('starter.nativeImage', async () => {
    const shot = await win.webContents.capturePage() // NativeImage
    const small = shot.resize({ width: 64 })
    const fromBuffer = nativeImage.createFromBuffer(shot.toPNG())
    const fromUrl = nativeImage.createFromDataURL(small.toDataURL())
    const cropped = shot.crop({ x: 0, y: 0, width: 16, height: 16 })
    return {
      original: shot.getSize(),
      resized: small.getSize(),
      cropped: cropped.getSize(),
      pngBytes: small.toPNG().length,
      reDecoded: fromBuffer.getSize(),
      fromDataURL: fromUrl.getSize(),
      empty: small.isEmpty(),
    }
  })

  // ── C5: menu (contextual + menubar) ──────────────────────────────────────
  // Menubar (Windows nativo; noop en Linux/GNOME por diseño).
  Menu.setApplicationMenu(
    Menu.buildFromTemplate([
      {
        label: 'Archivo',
        submenu: [
          { label: 'Nuevo', accelerator: 'CmdOrCtrl+N', click: () => void app.send('menu.event', 'Nuevo') },
          { type: 'separator' },
          { role: 'quit' },
        ],
      },
      {
        label: 'Ver',
        submenu: [
          {
            label: 'Barra lateral',
            type: 'checkbox',
            checked: true,
            click: (mi) => void app.send('menu.event', `sidebar=${mi.checked}`),
          },
          { type: 'separator' },
          { role: 'toggleDevTools' },
          { role: 'reload' },
        ],
      },
    ])
  )

  // Menú contextual (popup) disparado por un botón del starter.
  app.handle('starter.menu', () => {
    Menu.buildFromTemplate([
      { label: 'Nuevo', click: () => void app.send('menu.event', 'Nuevo') },
      { label: 'Abrir', accelerator: 'CmdOrCtrl+O', click: () => void app.send('menu.event', 'Abrir') },
      { type: 'separator' },
      {
        label: 'Barra lateral',
        type: 'checkbox',
        checked: true,
        click: (mi) => void app.send('menu.event', `checkbox=${mi.checked}`),
      },
      { label: 'Zona A', type: 'radio', checked: true, click: () => void app.send('menu.event', 'radio A') },
      { label: 'Zona B', type: 'radio', click: () => void app.send('menu.event', 'radio B') },
      { type: 'separator' },
      { role: 'copy' },
      { role: 'toggleDevTools' },
      { role: 'quit' },
    ]).popup({ window: win })
    return null
  })

  // ── C6: tray (icono de bandeja) ──────────────────────────────────────────
  let tray: Tray | null = null
  app.handle('starter.tray', async () => {
    if (tray) return { created: false, id: tray.id }
    const icon = (await win.webContents.capturePage()).resize({ width: 16, height: 16 })
    tray = new Tray(icon)
    tray.setToolTip('__APP_NAME__')
    tray.setTitle('__APP_NAME__')
    tray.setContextMenu(
      Menu.buildFromTemplate([
        { label: 'Mostrar ventana', click: () => void win.show() },
        { label: 'Capturar página', click: () => void app.send('tray.event', 'capturar') },
        { type: 'separator' },
        { role: 'quit' },
      ])
    )
    tray.on('click', () => {
      void win.show()
      void app.send('tray.event', 'click')
    })
    tray.on('right-click', () => void app.send('tray.event', 'right-click'))
    tray.on('double-click', () => void app.send('tray.event', 'double-click'))
    return { created: true, id: tray.id }
  })
  app.handle('starter.trayDestroy', () => {
    tray?.destroy()
    tray = null
    return null
  })

  // ── C7: nativeTheme (leer/forzar/escuchar) ───────────────────────────────
  app.handle('starter.themeGet', () => theme.get())
  app.handle('starter.themeSet', async (src: string) => {
    await theme.setSource((src as 'system' | 'light' | 'dark') ?? 'system')
    return theme.get()
  })
  void theme.watch() // cambios del sistema → ow.on('theme.changed')

  // ── C8: print / printToPDF ───────────────────────────────────────────────
  app.handle('starter.printPdf', async () => {
    try {
      const pdf = await win.webContents.printToPDF()
      const out = path.join(os.tmpdir(), 'owear-starter.pdf')
      fs.writeFileSync(out, pdf)
      void invokeNative('shell', 'showItemInFolder', out)
      return { ok: true, bytes: pdf.length, path: out }
    } catch (e) {
      return { ok: false, error: e instanceof Error ? e.message : String(e) }
    }
  })
  app.handle('starter.print', () => {
    win.webContents.print()
    return null
  })

  // ── C9: BrowserWindow completo ───────────────────────────────────────────
  let child: BrowserWindow | null = null
  app.handle('starter.childWindow', () => {
    if (child) {
      void child.focus()
      return { id: child.id }
    }
    child = new BrowserWindow({
      title: 'Ventana secundaria',
      width: 520,
      height: 360,
      parent: win.id ?? undefined,
      backgroundColor: '#12161c',
      url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
    })
    child.on('closed', () => {
      child = null
    })
    return { id: child.id }
  })
  app.handle('starter.toggleOnTop', async () => {
    const on = await win.isAlwaysOnTop()
    await win.setAlwaysOnTop(!on)
    return { on: !on }
  })
  app.handle('starter.progress', async () => {
    await win.setProgressBar(0.5)
    setTimeout(() => void win.setProgressBar(-1, { mode: 'none' }), 4000)
    return null
  })
  app.handle('starter.ignoreMouse', () => {
    void win.setIgnoreMouseEvents(true, { forward: true })
    setTimeout(() => void win.setIgnoreMouseEvents(false), 3000)
    return null
  })
  app.handle('starter.windows', async () => {
    const focus = await BrowserWindow.getFocusedWindow()
    const all = await BrowserWindow.getAllWindows()
    return { focused: focus?.id ?? null, count: all.length, ids: all.map((w) => w.id) }
  })

  // ── C10: pantallas (multi-monitor) y energía ─────────────────────────────
  app.handle('starter.screens', async () => {
    await screen.watch()
    const displays = await screen.getAllDisplays()
    const primary = displays.find((d) => d.primary) ?? displays[0] ?? null
    const cursor = await screen.getCursorScreenPoint()
    return { displays, primaryId: primary?.id ?? null, cursor }
  })
  app.handle('starter.nearest', async () => {
    const cursor = await screen.getCursorScreenPoint()
    const display = await screen.getDisplayNearestPoint(cursor)
    return { cursor, display }
  })
  app.handle('starter.powerOn', async () => {
    await powerMonitor.watch()
    const onBattery = await powerMonitor.isOnBatteryPower()
    const idle = await powerMonitor.getIdleTime()
    return { onBattery, idle }
  })
  app.handle('starter.idle', async () => {
    const idleTime = await powerMonitor.getIdleTime()
    const state = await powerMonitor.getIdleState(30)
    return { idleTime, state }
  })
  let blockerId: number | null = null
  app.handle('starter.blocker', () => {
    if (blockerId != null) {
      const ok = powerSaveBlocker.stop(blockerId)
      blockerId = null
      return { on: false, ok }
    }
    blockerId = powerSaveBlocker.start('prevent-display-sleep')
    return { on: true, id: blockerId }
  })
})

// ── C1: exponer `app` al renderer para probarlo en el starter ──────────────
// El renderer llama `ow.invoke('node','call',{fn:'starter.*'})`.
app.handle('starter.appInfo', () => ({
  name: app.getName(),
  version: app.getVersion(),
  packaged: app.isPackaged(),
  appPath: app.getAppPath(),
}))

const APP_PATH_NAMES = [
  'home',
  'userData',
  'temp',
  'logs',
  'downloads',
  'documents',
  'exe',
  'appPath',
] as const

app.handle('starter.appPaths', () => {
  const out: Record<string, string> = {}
  for (const name of APP_PATH_NAMES) out[name] = app.getPath(name)
  return out
})

// Ciclo de vida de la app → renderer (app.send → ow.on('app.event')).
for (const ev of [
  'window-all-closed',
  'before-quit',
  'will-quit',
  'second-instance',
  'child-process-gone',
] as const) {
  app.on(ev, () => app.send('app.event', ev))
}
