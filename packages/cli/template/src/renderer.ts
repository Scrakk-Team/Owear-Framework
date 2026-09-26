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

function boot(): void {
  wireTitlebar()
  renderEnvironment()

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
