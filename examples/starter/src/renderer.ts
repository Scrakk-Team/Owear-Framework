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
  void wireBrowser()

  for (const btn of document.querySelectorAll<HTMLElement>('[data-action]')) {
    const action = actions[btn.dataset.action ?? '']
    if (!action) continue
    btn.addEventListener('click', () => {
      Promise.resolve(action()).catch(logError)
    })
  }

  window.addEventListener('resize', renderEnvironment)
  window.ow.on('resize', renderEnvironment)

  log(`Renderer listo. Ventana ${window.__owWindowId}.`, 'ok')
}

boot()
