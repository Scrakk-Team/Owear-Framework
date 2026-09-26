// src/renderer.ts — el juego entero.
//
// Tesis del ejemplo: en Owear el renderer NO está incomunicado. Habla DIRECTO
// con los módulos nativos del kernel vía `ow.invoke()` (mismo proceso del
// WebView → kernel), sin pasar por un "main process" ni serializar nada.
//
// Por eso aquí no hay IPC por frame:
//   · el bucle del juego, la física y el dibujo corren en el renderer (canvas);
//   · sólo se cruza el bridge para cosas PUNTUALES del SO: leer/guardar el
//     récord (módulo stock `fs`) y notificar (módulo stock `notification`).
//
// Compáralo con Electron: allí cada acceso a fs/dialog/etc. vive en el main y
// exige `ipcRenderer.invoke(...)` → IPC → `ipcMain.handle(...)`, ida y vuelta.

declare global {
  interface Window {
    ow: {
      invoke(module: string, fn: string, ...args: unknown[]): Promise<unknown>
      on(name: string, cb: (payload: unknown) => void): () => void
      emit(name: string, payload?: unknown): void
      readShared(handle: { id: string; size: number }): Promise<ArrayBuffer>
    }
    __owWindowId: number
  }
}

const invoke = <T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T> =>
  window.ow.invoke(module, fn, ...args) as Promise<T>

// ── configuración ───────────────────────────────────────────────────────────
const COLS = 22
const ROWS = 22
const START_STEP = 130 // ms entre movimientos
const MIN_STEP = 68
const DECAY = 2.4 // ms menos por cada comida

type Vec = { x: number; y: number }
type Phase = 'ready' | 'playing' | 'paused' | 'over'

// ── estado ──────────────────────────────────────────────────────────────────
let body: Vec[] = []
let dir: Vec = { x: 1, y: 0 }
let queue: Vec[] = []
let food: Vec = { x: 0, y: 0 }
let score = 0
let best = 0
let stepMs = START_STEP
let acc = 0
let lastFrame = 0
let phase: Phase = 'ready'
let bestFile = ''

// ── DOM ─────────────────────────────────────────────────────────────────────
const stage = document.querySelector('.stage') as HTMLElement
const canvas = document.getElementById('board') as HTMLCanvasElement
const ctx = canvas.getContext('2d')!
const scoreEl = document.getElementById('score')!
const bestEl = document.getElementById('best')!
const speedEl = document.getElementById('speed')!
const overlay = document.getElementById('overlay')!
const overlayTitle = document.getElementById('overlay-title')!
const overlayText = document.getElementById('overlay-text')!

let cell = 20

function resize(): void {
  const availW = Math.max(160, stage.clientWidth - 32)
  const availH = Math.max(160, stage.clientHeight - 26)
  const size = Math.floor(Math.min(availW, availH))
  cell = Math.max(8, Math.floor(size / COLS))
  const w = cell * COLS
  const h = cell * ROWS
  const dpr = window.devicePixelRatio || 1
  canvas.style.width = `${w}px`
  canvas.style.height = `${h}px`
  canvas.width = Math.floor(w * dpr)
  canvas.height = Math.floor(h * dpr)
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
  draw()
}

// ── lógica ──────────────────────────────────────────────────────────────────
function spawnFood(): void {
  const taken = new Set(body.map((c) => c.y * COLS + c.x))
  const free: number[] = []
  for (let i = 0; i < COLS * ROWS; i++) if (!taken.has(i)) free.push(i)
  const pick = free.length ? free[(Math.random() * free.length) | 0] : 0
  food = { x: pick % COLS, y: (pick / COLS) | 0 }
}

function reset(): void {
  body = [
    { x: 8, y: 11 },
    { x: 7, y: 11 },
    { x: 6, y: 11 },
    { x: 5, y: 11 },
  ]
  dir = { x: 1, y: 0 }
  queue = []
  score = 0
  stepMs = START_STEP
  acc = 0
  spawnFood()
  updateHud()
}

function updateHud(): void {
  scoreEl.textContent = String(score)
  bestEl.textContent = String(best)
  speedEl.textContent = `${Math.round(1000 / stepMs)}/s`
}

function setPhase(p: Phase, title = '', text = ''): void {
  phase = p
  if (p === 'playing') {
    overlay.classList.add('hidden')
    return
  }
  overlayTitle.textContent = title
  overlayText.innerHTML = text
  overlay.classList.remove('hidden')
}

function step(): void {
  if (queue.length) {
    const next = queue.shift()!
    if (next.x !== -dir.x || next.y !== -dir.y) dir = next
  }
  const head: Vec = { x: body[0].x + dir.x, y: body[0].y + dir.y }
  const hitWall = head.x < 0 || head.y < 0 || head.x >= COLS || head.y >= ROWS
  const hitSelf = body.some(
    (c, i) => i < body.length - 1 && c.x === head.x && c.y === head.y
  )
  if (hitWall || hitSelf) return gameOver()

  body.unshift(head)
  if (head.x === food.x && head.y === food.y) {
    score++
    stepMs = Math.max(MIN_STEP, stepMs - DECAY)
    updateHud()
    spawnFood()
  } else {
    body.pop()
  }
}

function gameOver(): void {
  setPhase('over', 'Game over', `Puntos: <b>${score}</b><br>Enter para reiniciar`)
  if (score > best) {
    best = score
    updateHud()
    void persistBest(best)
    void notifyRecord(best)
  }
}

// ── dibujo ──────────────────────────────────────────────────────────────────
function path(x: number, y: number, w: number, h: number, r: number): void {
  ctx.beginPath()
  if (typeof (ctx as unknown as { roundRect?: unknown }).roundRect === 'function') {
    ctx.roundRect(x, y, w, h, r)
    return
  }
  ctx.moveTo(x + r, y)
  ctx.arcTo(x + w, y, x + w, y + h, r)
  ctx.arcTo(x + w, y + h, x, y + h, r)
  ctx.arcTo(x, y + h, x, y, r)
  ctx.arcTo(x, y, x + w, y, r)
  ctx.closePath()
}

function draw(): void {
  const w = COLS * cell
  const h = ROWS * cell
  ctx.clearRect(0, 0, w, h)
  ctx.fillStyle = '#0b0f14'
  ctx.fillRect(0, 0, w, h)

  // rejilla sutil
  ctx.strokeStyle = 'rgba(255,255,255,0.03)'
  ctx.lineWidth = 1
  for (let x = 1; x < COLS; x++) {
    ctx.beginPath()
    ctx.moveTo(x * cell + 0.5, 0)
    ctx.lineTo(x * cell + 0.5, h)
    ctx.stroke()
  }
  for (let y = 1; y < ROWS; y++) {
    ctx.beginPath()
    ctx.moveTo(0, y * cell + 0.5)
    ctx.lineTo(w, y * cell + 0.5)
    ctx.stroke()
  }

  // comida
  path(food.x * cell + 2, food.y * cell + 2, cell - 4, cell - 4, cell * 0.3)
  ctx.fillStyle = '#ff5d73'
  ctx.fill()

  // serpiente
  const n = Math.max(1, body.length - 1)
  body.forEach((c, i) => {
    path(c.x * cell + 1, c.y * cell + 1, cell - 2, cell - 2, cell * 0.28)
    if (i === 0) ctx.fillStyle = '#8affc1'
    else ctx.fillStyle = `rgba(74, 222, 128, ${0.92 - (i / n) * 0.55})`
    ctx.fill()
  })
}

// ── bucle (requestAnimationFrame: cero IPC) ─────────────────────────────────
function frame(t: number): void {
  requestAnimationFrame(frame)
  const dt = lastFrame ? t - lastFrame : 0
  lastFrame = t
  if (phase === 'playing') {
    acc += dt
    while (acc >= stepMs && phase === 'playing') {
      acc -= stepMs
      step()
    }
  }
  draw()
}

// ── entrada ─────────────────────────────────────────────────────────────────
const KEY_DIRS: Record<string, Vec> = {
  ArrowUp: { x: 0, y: -1 },
  ArrowDown: { x: 0, y: 1 },
  ArrowLeft: { x: -1, y: 0 },
  ArrowRight: { x: 1, y: 0 },
  w: { x: 0, y: -1 },
  s: { x: 0, y: 1 },
  a: { x: -1, y: 0 },
  d: { x: 1, y: 0 },
}

window.addEventListener('keydown', (e) => {
  if (e.key.startsWith('Arrow') || e.key === ' ') e.preventDefault()

  if (phase === 'ready' || phase === 'over') {
    if (e.key === 'Enter' || e.key === ' ' || e.key === 'r' || e.key === 'R') {
      reset()
      setPhase('playing')
    }
    return
  }

  if (phase === 'playing') {
    if (e.key === 'p' || e.key === 'P' || e.key === 'Escape') {
      setPhase('paused', 'Pausa', 'Pulsa <kbd>P</kbd> o <kbd>Espacio</kbd> para continuar')
      return
    }
    const d = KEY_DIRS[e.key] ?? KEY_DIRS[e.key.toLowerCase()]
    if (d && queue.length < 3) queue.push(d)
    return
  }

  if (phase === 'paused') {
    if (e.key === 'p' || e.key === 'P' || e.key === ' ' || e.key === 'Escape') {
      setPhase('playing')
    }
  }
})

// ── persistencia (módulo stock fs, directo) ─────────────────────────────────
async function loadBest(): Promise<number> {
  try {
    const txt = await invoke<string>('fs', 'readText', bestFile)
    return parseInt(txt, 10) || 0
  } catch {
    return 0
  }
}

async function persistBest(n: number): Promise<void> {
  try {
    // writeFile crea los directorios padre que falten.
    await invoke('fs', 'writeFile', bestFile, String(n), 'utf8')
  } catch {
    /* sin persistencia disponible */
  }
}

async function notifyRecord(n: number): Promise<void> {
  try {
    await invoke('notification', 'show', '¡Nuevo récord!', `${n} puntos en Owear Snake`, 'Owear Snake')
  } catch {
    /* sin bus de notificaciones (p. ej. sin sesión D-Bus) */
  }
}

async function initPersistence(): Promise<void> {
  try {
    const dir = await invoke<string>('path', 'userDataDir')
    bestFile = await invoke<string>('path', 'join', dir, 'owear-snake', 'best.txt')
    best = await loadBest()
    updateHud()
  } catch (err) {
    console.warn('[snake] persistencia no disponible:', err)
  }
}

// ── controles de ventana (ow-window, builtin del kernel) ────────────────────
function wireControls(): void {
  const id = window.__owWindowId
  const bind = (el: string, fn: () => void): void =>
    document.getElementById(el)?.addEventListener('click', fn)

  bind('btn-min', () => void invoke('ow-window', 'minimize', id))
  bind('btn-max', () => {
    void invoke<boolean>('ow-window', 'isMaximized', id).then((max) =>
      invoke('ow-window', 'maximize', id, !max)
    )
  })
  bind('btn-close', () => void invoke('ow-window', 'close', id))
}

// ── arranque ────────────────────────────────────────────────────────────────
async function boot(): Promise<void> {
  reset()
  wireControls()
  window.addEventListener('resize', resize)
  window.ow.on('resize', () => resize())
  resize()
  setPhase(
    'ready',
    '🐍 Owear Snake',
    'Flechas o <kbd>WASD</kbd> para moverte · <kbd>Enter</kbd> o <kbd>Espacio</kbd> para empezar' +
      '<br><span class="muted">El juego corre 100% en el renderer — cero IPC por frame.</span>'
  )
  await initPersistence()
  requestAnimationFrame(frame)
}

void boot()
