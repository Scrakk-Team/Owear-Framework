// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/renderer.ts — corre dentro del WebView. `ow` lo inyecta el kernel.
//
// Aquí no hay `ipcRenderer` ni handlers en el main: el renderer llama a los
// módulos nativos del framework DIRECTO (mismo salto WebView → kernel).

import './style.css'

const APP = document.title || 'Owear App'

const $ = <T extends HTMLElement = HTMLElement>(sel: string): T =>
  document.querySelector(sel) as T

const invoke = <T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T> =>
  window.ow.invoke(module, fn, ...args) as Promise<T>

let lastPath: string | null = null

// ── Consola ─────────────────────────────────────────────────────────────
const consoleEl = $('#console')

function log(msg: string, kind: 'ok' | 'err' | 'muted' = 'muted'): void {
  const time = new Date().toLocaleTimeString()
  const line = document.createElement('span')
  line.className = `line--${kind}`
  line.textContent = `[${time}] ${msg}\n`
  consoleEl.appendChild(line)
  consoleEl.scrollTop = consoleEl.scrollHeight
}

function logError(err: unknown): void {
  log(err instanceof Error ? err.message : String(err), 'err')
}

// ── Entorno ─────────────────────────────────────────────────────────────
function renderEnvironment(): void {
  const ua = navigator.userAgent
  const engine = /AppleWebKit\/([\d.]+)/.exec(ua)?.[1]
  $('#env-platform').textContent = navigator.platform || '—'
  $('#env-engine').textContent = engine ? `WebKit ${engine}` : '—'
  $('#env-dpr').textContent = `${window.devicePixelRatio}×`
  $('#env-window').textContent = `#${window.__owWindowId}`
  $('#env-ua').textContent = ua
}

// ── App (C1): rutas/identidad vía el puente Node + eventos de ciclo de vida ─
async function renderApp(): Promise<void> {
  try {
    const info = await invoke<Record<string, unknown>>('node', 'call', {
      fn: 'starter.appInfo',
      args: [],
    })
    $('#app-name').textContent = String(info.name ?? '—')
    $('#app-version').textContent = String(info.version ?? '—')
    $('#app-packaged').textContent = info.packaged ? 'sí' : 'no'

    const paths = await invoke<Record<string, string>>('node', 'call', {
      fn: 'starter.appPaths',
      args: [],
    })
    for (const name of ['userData', 'exe', 'appPath']) {
      const el = document.getElementById(`app-path-${name}`)
      if (el) el.textContent = paths[name] ?? '—'
    }
  } catch (e) {
    log(`app.* no disponible: ${e}`, 'err')
  }
}

// ── Acciones (módulos nativos) ──────────────────────────────────────────
async function openFile(): Promise<void> {
  const path = await invoke<string | null>('dialog', 'open', 'open', 'Abrir un archivo')
  if (!path) {
    log('dialog.open → cancelado')
    return
  }
  lastPath = path
  const stat = await invoke<{ size: number }>('fs', 'stat', path)
  const text = await invoke<string>('fs', 'readText', path)
  log(`dialog.open → ${path}`, 'ok')
  log(`fs.readText → ${stat.size} bytes`, 'ok')

  const clean = text.replace(/\u0000/g, '')
  const preview = clean.length > 6000 ? `${clean.slice(0, 6000)}\n… (vista truncada)` : clean
  consoleEl.textContent = ''
  log(`── ${path} (${stat.size} bytes) ──`, 'muted')
  const body = document.createElement('span')
  body.style.color = '#c8d4e0'
  body.textContent = preview + '\n'
  consoleEl.appendChild(body)
  consoleEl.scrollTop = 0
}

async function notifyUser(): Promise<void> {
  await invoke('notification', 'show', APP, 'Los módulos nativos funcionan sin IPC.', APP)
  log('notification.show → notificación enviada', 'ok')
}

async function copyPath(): Promise<void> {
  if (!lastPath) {
    log('Abre un archivo antes de copiar.', 'err')
    return
  }
  await invoke('clipboard', 'writeText', lastPath)
  log(`clipboard.writeText → ${lastPath}`, 'ok')
}

async function revealPath(): Promise<void> {
  if (!lastPath) {
    log('Abre un archivo antes de mostrarlo.', 'err')
    return
  }
  await invoke('shell', 'showItemInFolder', lastPath)
  log(`shell.showItemInFolder → ${lastPath}`, 'ok')
}

async function about(): Promise<void> {
  await invoke('dialog', 'messageBox', 'info', `Acerca de ${APP}`, `${APP}\n\nConstruido con Owear.`, [
    'Cerrar',
  ])
  log('dialog.messageBox → cerrado', 'ok')
}

// ── C2: diálogos completos (showOpenDialog/showSaveDialog/showMessageBox) ──
async function openMultiple(): Promise<void> {
  const r = await invoke<{ canceled: boolean; filePaths: string[] }>(
    'dialog',
    'showOpenDialog',
    {
      title: 'Abrir varios',
      properties: ['openFile', 'multiSelections'],
      filters: [{ name: 'Texto', extensions: ['txt', 'md', 'json'] }],
    },
  )
  if (r.canceled) return log('showOpenDialog → cancelado')
  log(`showOpenDialog → ${r.filePaths.length} archivo(s)`, 'ok')
  for (const p of r.filePaths.slice(0, 4)) log(`  · ${p}`)
}

async function saveAs(): Promise<void> {
  const r = await invoke<{ canceled: boolean; filePath: string }>('dialog', 'showSaveDialog', {
    title: 'Guardar como',
    defaultPath: `owear-${Date.now()}.txt`,
    filters: [{ name: 'Texto', extensions: ['txt', 'md'] }],
  })
  if (r.canceled) return log('showSaveDialog → cancelado')
  lastPath = r.filePath
  log(`showSaveDialog → ${r.filePath}`, 'ok')
}

// ── C3: webContents ──────────────────────────────────────────────────────
async function capturePage(): Promise<void> {
  const r = await invoke<{ data: string }>('node', 'call', {
    fn: 'starter.capture',
    args: [],
  })
  log(`webContents.capturePage → ${Math.round((r.data.length * 3) / 4)} bytes PNG`, 'ok')
}

// ── C4: nativeImage ──────────────────────────────────────────────────────
async function nativeImageTest(): Promise<void> {
  const r = await invoke<{
    original: { width: number; height: number }
    resized: { width: number; height: number }
    cropped: { width: number; height: number }
    pngBytes: number
    fromDataURL: { width: number; height: number }
  }>('node', 'call', { fn: 'starter.nativeImage', args: [] })
  log(
    `nativeImage → ${r.original.width}×${r.original.height} · resize ${r.resized.width}×${r.resized.height} · crop ${r.cropped.width}×${r.cropped.height} · ${r.pngBytes} B · dataURL ${r.fromDataURL.width}×${r.fromDataURL.height}`,
    'ok',
  )
}

// ── C5: menú contextual ──────────────────────────────────────────────────
async function showMenu(): Promise<void> {
  await invoke('node', 'call', { fn: 'starter.menu', args: [] })
  log('menu.popup → abierto (elige una opción)', 'ok')
}

// ── C6: tray ─────────────────────────────────────────────────────────────
async function toggleTray(): Promise<void> {
  const r = await invoke<{ created: boolean; id: string }>('node', 'call', {
    fn: 'starter.tray',
    args: [],
  })
  log(r.created ? `tray creado (${r.id})` : `tray ya existe (${r.id})`, 'ok')
}

// ── C7: nativeTheme ──────────────────────────────────────────────────────
function applyTheme(dark: boolean): void {
  document.documentElement.dataset.theme = dark ? 'dark' : 'light'
}

async function loadTheme(): Promise<void> {
  try {
    const info = await invoke<{ dark: boolean; source: string }>('node', 'call', {
      fn: 'starter.themeGet',
      args: [],
    })
    applyTheme(!!info.dark)
    log(`theme.get → dark=${info.dark} source=${info.source}`, 'muted')
  } catch (e) {
    logError(e)
  }
}

async function setTheme(source: string): Promise<void> {
  const info = await invoke<{ dark: boolean; source: string }>('node', 'call', {
    fn: 'starter.themeSet',
    args: [source],
  })
  applyTheme(!!info.dark)
  log(`theme.setSource('${source}') → dark=${info.dark}`, 'ok')
}

// ── C8: print / printToPDF ───────────────────────────────────────────────
async function exportPdf(): Promise<void> {
  const r = await invoke<{ ok: boolean; bytes?: number; path?: string; error?: string }>(
    'node',
    'call',
    { fn: 'starter.printPdf', args: [] },
  )
  if (r.ok) log(`printToPDF → ${r.bytes} bytes → ${r.path}`, 'ok')
  else log(`printToPDF no disponible: ${r.error}`, 'err')
}

async function printDoc(): Promise<void> {
  await invoke('node', 'call', { fn: 'starter.print', args: [] })
  log('print → diálogo de impresión abierto', 'ok')
}

// ── C9: BrowserWindow completo ───────────────────────────────────────────
async function childWindow(): Promise<void> {
  const r = await invoke<{ id: number }>('node', 'call', {
    fn: 'starter.childWindow',
    args: [],
  })
  log(`ventana secundaria → #${r.id} (parent + backgroundColor)`, 'ok')
}
async function toggleOnTop(): Promise<void> {
  const r = await invoke<{ on: boolean }>('node', 'call', {
    fn: 'starter.toggleOnTop',
    args: [],
  })
  log(`setAlwaysOnTop(${r.on})`, 'ok')
}
async function progressBar(): Promise<void> {
  await invoke('node', 'call', { fn: 'starter.progress', args: [] })
  log('setProgressBar(0.5) → barra de tareas', 'ok')
}
async function ignoreMouse(): Promise<void> {
  await invoke('node', 'call', { fn: 'starter.ignoreMouse', args: [] })
  log('setIgnoreMouseEvents(true) 3 s…', 'muted')
}
async function listWindows(): Promise<void> {
  const r = await invoke<{ focused: number | null; count: number; ids: number[] }>(
    'node',
    'call',
    { fn: 'starter.windows', args: [] },
  )
  log(`windows → foco=#${r.focused} total=${r.count} ids=[${r.ids}]`, 'ok')
}

// ── C10: pantallas (multi-monitor) y energía ─────────────────────────────
interface DisplayRect {
  x: number
  y: number
  width: number
  height: number
}
interface Display {
  id: number
  primary: boolean
  label: string
  bounds: DisplayRect
  size: { width: number; height: number }
  workArea: DisplayRect
  workAreaSize: { width: number; height: number }
  scaleFactor: number
  rotation: number
  internal: boolean
}

function renderMonitorMap(displays: Display[], cursor: { x: number; y: number }): void {
  const map = $('#mon-map')
  map.textContent = ''
  if (!displays.length) {
    map.innerHTML = '<span class="monmap__hint">Sin monitores.</span>'
    return
  }
  const minX = Math.min(...displays.map((d) => d.bounds.x))
  const minY = Math.min(...displays.map((d) => d.bounds.y))
  const maxX = Math.max(...displays.map((d) => d.bounds.x + d.bounds.width))
  const maxY = Math.max(...displays.map((d) => d.bounds.y + d.bounds.height))
  const w = maxX - minX || 1
  const h = maxY - minY || 1
  const pad = 6
  const span = 100 - pad * 2
  const sx = (px: number): number => pad + ((px - minX) / w) * span
  const sy = (py: number): number => pad + ((py - minY) / h) * span

  for (const d of displays) {
    const cell = document.createElement('div')
    cell.className = `monmap__cell${d.primary ? ' monmap__cell--primary' : ''}`
    cell.style.left = `${sx(d.bounds.x)}%`
    cell.style.top = `${sy(d.bounds.y)}%`
    cell.style.width = `${(d.bounds.width / w) * span}%`
    cell.style.height = `${(d.bounds.height / h) * span}%`
    const rot = d.rotation ? ` · ${d.rotation}°` : ''
    cell.innerHTML =
      `<span class="monmap__id">#${d.id}${d.primary ? ' ★' : ''}</span>` +
      `<span class="monmap__meta">${d.size.width}×${d.size.height} · ${d.scaleFactor}×${rot}</span>`
    map.appendChild(cell)
  }

  const cur = document.createElement('span')
  cur.className = 'monmap__cursor'
  cur.style.left = `${sx(cursor.x)}%`
  cur.style.top = `${sy(cursor.y)}%`
  map.appendChild(cur)

  $('#mon-count').textContent = `${displays.length} monitor(es)`
  const p = displays.find((d) => d.primary)
  $('#mon-primary').textContent = p ? `primario #${p.id}` : '—'
  $('#mon-list').innerHTML = displays
    .map(
      (d) =>
        `<div class="monlist__row"><code>#${d.id}</code> ${d.label ? `${d.label} · ` : ''}` +
        `${d.bounds.x},${d.bounds.y} · ${d.bounds.width}×${d.bounds.height}` +
        ` · work ${d.workArea.width}×${d.workArea.height} · ${d.scaleFactor}×` +
        `${d.internal ? ' · interno' : ''}</div>`,
    )
    .join('')
}

async function showScreens(): Promise<void> {
  const r = await invoke<{
    displays: Display[]
    primaryId: number | null
    cursor: { x: number; y: number }
  }>('node', 'call', { fn: 'starter.screens', args: [] })
  renderMonitorMap(r.displays, r.cursor)
  log(`screen → ${r.displays.length} monitor(es), primario #${r.primaryId}`, 'ok')
}

async function nearestDisplay(): Promise<void> {
  const r = await invoke<{ cursor: { x: number; y: number }; display: Display | null }>(
    'node',
    'call',
    { fn: 'starter.nearest', args: [] },
  )
  log(
    `screen.nearest → cursor ${r.cursor.x},${r.cursor.y} ⇒ display #${r.display?.id ?? '—'}`,
    'ok',
  )
}

let powerLogReady = false
function appendPower(ev: string): void {
  const el = $('#power-log')
  if (!powerLogReady) {
    el.textContent = ''
    powerLogReady = true
  }
  el.textContent += `[${new Date().toLocaleTimeString()}] ${ev}\n`
  el.scrollTop = el.scrollHeight
}

async function powerOn(): Promise<void> {
  const r = await invoke<{ onBattery: boolean; idle: number }>('node', 'call', {
    fn: 'starter.powerOn',
    args: [],
  })
  $('#power-status').textContent = r.onBattery ? 'batería' : 'AC'
  appendPower(`powerMonitor ON · onBattery=${r.onBattery} idle=${r.idle}s`)
  log(`powerMonitor → onBattery=${r.onBattery} idle=${r.idle}s`, 'ok')
}

async function toggleBlocker(): Promise<void> {
  const r = await invoke<{ on: boolean; id?: number }>('node', 'call', {
    fn: 'starter.blocker',
    args: [],
  })
  $('#power-status').textContent = r.on ? 'pantalla activa' : 'bloqueador OFF'
  log(`powerSaveBlocker → ${r.on ? `ON (id ${r.id})` : 'OFF'}`, 'ok')
}

async function idleInfo(): Promise<void> {
  const r = await invoke<{ idleTime: number; state: string }>('node', 'call', {
    fn: 'starter.idle',
    args: [],
  })
  log(`powerMonitor.idle → ${r.idleTime}s · estado «${r.state}»`, 'ok')
}

async function askDialog(): Promise<void> {
  const r = await invoke<{ response: number; checkboxChecked: boolean }>(
    'dialog',
    'showMessageBox',
    {
      type: 'question',
      title: 'C2',
      message: '¿Continuar?',
      detail: 'showMessageBox con botones y checkbox.',
      buttons: ['Sí', 'No', 'Cancelar'],
      defaultId: 0,
      cancelId: 2,
      checkboxLabel: 'No volver a preguntar',
    },
  )
  log(`showMessageBox → response=${r.response} checkbox=${r.checkboxChecked}`, 'ok')
}

// ── Titlebar (builtin ow-window) ────────────────────────────────────────
/**
 * Si el SO provee botones de ventana nativos (titleBarOverlay), ocultamos los
 * del web y reservamos su hueco. La clase y las CSS vars las consume style.css.
 */
function applyTitleBarOverlay(): void {
  const ov = window.__owTitlebarOverlay
  const root = document.documentElement
  if (ov?.enabled) {
    root.classList.add('ow-native-overlay')
    root.style.setProperty('--ow-overlay-height', `${ov.height}px`)
    root.style.setProperty('--ow-overlay-width', `${ov.width}px`)
  } else {
    // Sin botones nativos: mostramos los del web (.ow-web-controls).
    root.classList.add('ow-web-controls')
  }
}

function wireTitlebar(): void {
  const id = window.__owWindowId
  $('#win-min').addEventListener('click', () => void invoke('ow-window', 'minimize', id))
  $('#win-max').addEventListener('click', async () => {
    const max = await invoke<boolean>('ow-window', 'isMaximized', id)
    void invoke('ow-window', 'maximize', id, !max)
  })
  $('#win-close').addEventListener('click', () => void invoke('ow-window', 'close', id))
}

// ── Arranque ────────────────────────────────────────────────────────────
const actions: Record<string, () => void | Promise<void>> = {
  open: openFile,
  multi: openMultiple,
  save: saveAs,
  capture: capturePage,
  nimage: nativeImageTest,
  menu: showMenu,
  tray: toggleTray,
  'theme-system': () => setTheme('system'),
  'theme-light': () => setTheme('light'),
  'theme-dark': () => setTheme('dark'),
  pdf: exportPdf,
  print: printDoc,
  child: childWindow,
  ontop: toggleOnTop,
  prog: progressBar,
  ignore: ignoreMouse,
  wins: listWindows,
  screens: showScreens,
  nearest: nearestDisplay,
  'power-on': powerOn,
  blocker: toggleBlocker,
  idle: idleInfo,
  ask: askDialog,
  notify: notifyUser,
  copy: copyPath,
  reveal: revealPath,
  about,
  clear: () => {
    consoleEl.textContent = ''
    log('Consola limpia.')
  },
}

// ── Navegador embebido (webview nativa vía API `webview.*`) ─────────────
let wvId: number | null = null

/** Coloca la webview nativa sobre #wv-slot (y la oculta si el slot sale de vista). */
function syncBrowserBounds(): void {
  if (wvId == null) return
  const slot = document.getElementById('wv-slot')
  if (!slot) return
  const r = slot.getBoundingClientRect()
  const onScreen =
    r.bottom > 0 && r.top < window.innerHeight && r.width > 8 && r.height > 8
  void invoke('webview', 'setBounds', wvId, {
    x: Math.round(r.left),
    y: Math.round(r.top),
    width: Math.round(r.width),
    height: Math.round(r.height),
  })
  void invoke('webview', 'setVisible', wvId, onScreen)
}

async function wireBrowser(): Promise<void> {
  const urlInput = $('#wv-url') as HTMLInputElement
  const status = $('#wv-status')
  try {
    const res = await invoke<{ id: number }>('webview', 'create', {
      url: urlInput.value,
      x: 0,
      y: 0,
      width: 100,
      height: 100,
    })
    wvId = res.id
  } catch (e) {
    log(`webview no disponible: ${e}`, 'err')
    status.textContent = 'no disponible'
    return
  }

  const nav = (op: string, ...args: unknown[]): void => {
    if (wvId != null) void invoke('webview', op, wvId, ...args)
  }
  const go = (): void => {
    let u = urlInput.value.trim()
    if (u && !/^[a-z][a-z0-9+.-]*:\/\//i.test(u)) u = `https://${u}`
    nav('load', u)
  }

  $('#wv-back').addEventListener('click', () => nav('back'))
  $('#wv-fwd').addEventListener('click', () => nav('forward'))
  $('#wv-reload').addEventListener('click', () => nav('reload'))
  $('#wv-go').addEventListener('click', go)
  urlInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') go()
  })

  window.ow.on('webview.urlChanged', (p) => {
    const d = p as { id: number; url: string }
    if (d.id === wvId && d.url) urlInput.value = d.url
  })
  window.ow.on('webview.titleChanged', (p) => {
    const d = p as { id: number; title: string }
    if (d.id === wvId) status.textContent = d.title || '—'
  })
  window.ow.on('webview.loadChanged', (p) => {
    const d = p as { id: number; state: string }
    if (d.id === wvId) status.textContent = d.state
  })
  window.ow.on('webview.loadFailed', (p) => {
    const d = p as { id: number; message: string }
    if (d.id === wvId) log(`webview error: ${d.message}`, 'err')
  })

  const page = $('.page')
  page.addEventListener('scroll', syncBrowserBounds, { passive: true } as AddEventListenerOptions)
  window.addEventListener('resize', syncBrowserBounds)
  requestAnimationFrame(syncBrowserBounds)
}

function boot(): void {
  applyTitleBarOverlay()
  wireTitlebar()
  renderEnvironment()
  void renderApp()
  void loadTheme()
  void wireBrowser()

  // Eventos de ciclo de vida de la app (C1): pueden llegar en cualquier momento.
  window.ow.on('app.event', (name) => log(`app.event → ${name}`, 'ok'))
  // Eventos de webContents (C3) reenviados por el main.
  window.ow.on('webContents.event', (name) => log(`webContents → ${name}`, 'ok'))
  // Clicks de menú (C5).
  window.ow.on('menu.event', (name) => log(`menu → ${name}`, 'ok'))
  // Eventos del tray (C6).
  window.ow.on('tray.event', (b) => log(`tray → ${b}`, 'ok'))
  // Cambios de tema (C7).
  window.ow.on('theme.changed', (t) => {
    const dark = (t as { dark?: boolean })?.dark
    applyTheme(!!dark)
    log(`theme.changed → dark=${dark}`, 'ok')
  })

  // C10 — pantallas: eventos nativos (requieren haber pulsado «Pantallas»).
  window.ow.on('screen.added', (p) => {
    log(`screen.added → #${(p as { display?: { id?: number } })?.display?.id}`, 'ok')
    void showScreens()
  })
  window.ow.on('screen.removed', (p) => {
    log(`screen.removed → #${(p as { display?: { id?: number } })?.display?.id}`, 'ok')
    void showScreens()
  })
  window.ow.on('screen.changed', (p) => {
    const d = p as { display?: { id?: number }; metrics?: string[] }
    log(`screen.changed → #${d?.display?.id} [${(d?.metrics ?? []).join(', ')}]`, 'ok')
    void showScreens()
  })

  // C10 — energía.
  window.ow.on('power.suspend', () => appendPower('suspend'))
  window.ow.on('power.resume', () => appendPower('resume'))
  window.ow.on('power.ac', () => {
    $('#power-status').textContent = 'AC'
    appendPower('on-ac')
  })
  window.ow.on('power.battery', () => {
    $('#power-status').textContent = 'batería'
    appendPower('on-battery')
  })
  window.ow.on('power.shutdown', () => appendPower('shutdown'))
  window.ow.on('power.lock', () => appendPower('lock-screen'))
  window.ow.on('power.unlock', () => appendPower('unlock-screen'))

  for (const btn of document.querySelectorAll<HTMLElement>('[data-action]')) {
    const action = actions[btn.dataset.action ?? '']
    if (!action) continue
    let busy = false
    btn.addEventListener('click', () => {
      if (busy) return
      busy = true
      Promise.resolve(action())
        .catch(logError)
        .finally(() => {
          busy = false
        })
    })
  }

  window.addEventListener('resize', renderEnvironment)
  window.ow.on('resize', renderEnvironment)

  log(`Renderer listo. Ventana ${window.__owWindowId}.`, 'ok')
}

boot()
