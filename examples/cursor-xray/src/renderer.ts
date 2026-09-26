// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/renderer.ts — inspector de pantalla y cursor.
//
// Qué demuestra:
//   1. El cursor GLOBAL no se puede leer desde el DOM (el DOM sólo ve el cursor
//      dentro de la ventana). Lo expone un MÓDULO NATIVO stock: `screen`.
//      Botón "Seguir cursor" → `ow.invoke('screen','getCursorScreenPoint')`.
//   2. Los monitores también son nativos: `screen.getAllDisplays()`.
//   3. Cuando una capacidad NO está en el stock, escribes tu .owm: aquí
//      `hostinfo.info()` (hostname, CPUs, RAM, uptime) leído del SO en C++.
//
// Ninguna de estas llamadas pasa por un "main process": van directas del
// WebView al kernel.

import { hostinfo } from '@owear/native'

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

// ── tipos ───────────────────────────────────────────────────────────────────
type Rect = { x: number; y: number; width: number; height: number }
type Display = {
  id: number
  primary: boolean
  bounds: Rect
  workArea: Rect
  scaleFactor: number
}
type Point = { x: number; y: number }
type HostInfo = {
  os: string
  hostname: string
  release: string
  arch: string
  cpuCount: number
  totalMemMb: number
  freeMemMb: number
  uptimeSec: number
  pid: number
}

// ── estado ──────────────────────────────────────────────────────────────────
let displays: Display[] = []
let cursor: Point = { x: 0, y: 0 }
let pinned: Point | null = null
let follow = false
let pollTimer: number | null = null
let busy = false

// ── DOM ─────────────────────────────────────────────────────────────────────
const canvas = document.getElementById('view') as HTMLCanvasElement
const ctx = canvas.getContext('2d')!
const cxEl = document.getElementById('cx')!
const cyEl = document.getElementById('cy')!
const monEl = document.getElementById('mon')!
const pinEl = document.getElementById('pin')!
const sysEl = document.getElementById('sysinfo')!

// ── helpers ─────────────────────────────────────────────────────────────────
function monitorAt(x: number, y: number): Display | null {
  for (const d of displays) {
    const b = d.bounds
    if (x >= b.x && x < b.x + b.width && y >= b.y && y < b.y + b.height) return d
  }
  return null
}

function updateReadout(): void {
  cxEl.textContent = String(Math.round(cursor.x))
  cyEl.textContent = String(Math.round(cursor.y))
  const m = monitorAt(cursor.x, cursor.y)
  monEl.textContent = m ? `#${m.id}${m.primary ? ' (primaria)' : ''} · ${m.bounds.width}×${m.bounds.height} @${m.scaleFactor}x` : '—'
  pinEl.textContent = pinned ? `${pinned.x}, ${pinned.y}` : '—'
}

// ── dibujo ──────────────────────────────────────────────────────────────────
function roundRect(x: number, y: number, w: number, h: number, r: number): void {
  ctx.beginPath()
  const anyCtx = ctx as unknown as { roundRect?: unknown }
  if (typeof anyCtx.roundRect === 'function') {
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
  const dpr = window.devicePixelRatio || 1
  const cw = canvas.clientWidth
  const ch = canvas.clientHeight
  canvas.width = Math.max(1, Math.floor(cw * dpr))
  canvas.height = Math.max(1, Math.floor(ch * dpr))
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
  ctx.clearRect(0, 0, cw, ch)

  if (!displays.length) {
    ctx.fillStyle = '#7d8b99'
    ctx.font = '14px system-ui'
    ctx.fillText('Sin monitores (¿módulo screen cargado?)', 20, 30)
    return
  }

  const pad = 26
  const minX = Math.min(...displays.map((d) => d.bounds.x))
  const minY = Math.min(...displays.map((d) => d.bounds.y))
  const maxX = Math.max(...displays.map((d) => d.bounds.x + d.bounds.width))
  const maxY = Math.max(...displays.map((d) => d.bounds.y + d.bounds.height))
  const worldW = Math.max(1, maxX - minX)
  const worldH = Math.max(1, maxY - minY)
  const scale = Math.min((cw - pad * 2) / worldW, (ch - pad * 2) / worldH)
  const ox = pad + (cw - pad * 2 - worldW * scale) / 2 - minX * scale
  const oy = pad + (ch - pad * 2 - worldH * scale) / 2 - minY * scale
  const mapX = (x: number): number => ox + x * scale
  const mapY = (y: number): number => oy + y * scale

  // monitores
  for (const d of displays) {
    const b = d.bounds
    const x = mapX(b.x)
    const y = mapY(b.y)
    const w = b.width * scale
    const h = b.height * scale

    roundRect(x, y, w, h, 10)
    ctx.fillStyle = '#0f1720'
    ctx.fill()
    ctx.lineWidth = 1.5
    ctx.strokeStyle = d.primary ? '#2f6b41' : '#2b3946'
    ctx.stroke()

    // área de trabajo
    const wa = d.workArea
    ctx.setLineDash([4, 4])
    ctx.strokeStyle = 'rgba(255,255,255,0.10)'
    ctx.lineWidth = 1
    ctx.strokeRect(mapX(wa.x), mapY(wa.y), wa.width * scale, wa.height * scale)
    ctx.setLineDash([])

    ctx.fillStyle = '#7d8b99'
    ctx.font = '12px system-ui'
    ctx.fillText(`#${d.id}${d.primary ? ' ★' : ''}`, x + 10, y + 18)
    ctx.fillStyle = '#4b5a68'
    ctx.fillText(`${b.width}×${b.height} @${d.scaleFactor}x`, x + 10, y + 34)
  }

  // punto fijado
  if (pinned) {
    const px = mapX(pinned.x)
    const py = mapY(pinned.y)
    ctx.strokeStyle = '#ffb454'
    ctx.lineWidth = 1.5
    ctx.beginPath()
    ctx.moveTo(px - 10, py)
    ctx.lineTo(px + 10, py)
    ctx.moveTo(px, py - 10)
    ctx.lineTo(px, py + 10)
    ctx.stroke()
    ctx.beginPath()
    ctx.arc(px, py, 5, 0, Math.PI * 2)
    ctx.stroke()
    ctx.fillStyle = '#ffb454'
    ctx.font = '12px ui-monospace, monospace'
    ctx.fillText(`${pinned.x},${pinned.y}`, px + 12, py - 8)
  }

  // cursor en vivo
  const gx = mapX(cursor.x)
  const gy = mapY(cursor.y)
  ctx.beginPath()
  ctx.arc(gx, gy, 9, 0, Math.PI * 2)
  ctx.fillStyle = 'rgba(138,255,193,0.18)'
  ctx.fill()
  ctx.beginPath()
  ctx.arc(gx, gy, 4.5, 0, Math.PI * 2)
  ctx.fillStyle = follow ? '#8affc1' : '#4b5a68'
  ctx.fill()
}

// ── muestreo nativo del cursor ──────────────────────────────────────────────
async function sampleCursor(): Promise<void> {
  if (busy) return
  busy = true
  try {
    const p = await invoke<Point>('screen', 'getCursorScreenPoint')
    if (p && typeof p.x === 'number' && typeof p.y === 'number') cursor = p
    updateReadout()
    draw()
  } catch (err) {
    console.warn('[cursor-xray] getCursorScreenPoint:', err)
    stopFollow()
  } finally {
    busy = false
  }
}

function startFollow(): void {
  follow = true
  document.getElementById('btn-follow')!.classList.add('on')
  if (pollTimer == null) pollTimer = window.setInterval(sampleCursor, 60)
}

function stopFollow(): void {
  follow = false
  document.getElementById('btn-follow')!.classList.remove('on')
  if (pollTimer != null) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

// ── acciones ────────────────────────────────────────────────────────────────
function wireActions(): void {
  document.getElementById('btn-follow')!.addEventListener('click', () => {
    if (follow) stopFollow()
    else startFollow()
  })

  document.getElementById('btn-pin')!.addEventListener('click', async () => {
    stopFollow()
    await sampleCursor()
    pinned = { x: Math.round(cursor.x), y: Math.round(cursor.y) }
    updateReadout()
    draw()
  })

  document.getElementById('btn-copy')!.addEventListener('click', async () => {
    const text = `${Math.round(cursor.x)}, ${Math.round(cursor.y)}`
    try {
      await invoke('clipboard', 'writeText', text)
    } catch (err) {
      console.warn('[cursor-xray] clipboard:', err)
    }
  })

  document.getElementById('btn-notify')!.addEventListener('click', async () => {
    try {
      await invoke(
        'notification',
        'show',
        'Cursor X-Ray',
        `Cursor en ${Math.round(cursor.x)}, ${Math.round(cursor.y)}`,
        'Cursor X-Ray'
      )
    } catch (err) {
      console.warn('[cursor-xray] notification:', err)
    }
  })

  document.getElementById('btn-sysinfo')!.addEventListener('click', async () => {
    try {
      const info = await hostinfo.info()
      const h = info as HostInfo
      const lines = [
        `os        ${h.os} ${h.release} (${h.arch})`,
        `hostname  ${h.hostname}`,
        `cpuCount  ${h.cpuCount}`,
        `memoria   ${h.totalMemMb} MB total · ${h.freeMemMb} MB libre`,
        `uptime    ${(h.uptimeSec / 3600).toFixed(1)} h`,
        `pid       ${h.pid}`,
      ]
      sysEl.textContent = lines.join('\n')
    } catch (err) {
      sysEl.textContent = `hostinfo falló: ${String(err)}\n(¿compilaste native/hostinfo.cpp?)`
    }
  })
}

// ── controles de ventana ────────────────────────────────────────────────────
function wireWindowControls(): void {
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
  wireActions()
  wireWindowControls()
  window.addEventListener('resize', draw)
  window.ow.on('resize', () => draw())

  try {
    displays = await invoke<Display[]>('screen', 'getAllDisplays')
  } catch (err) {
    console.warn('[cursor-xray] getAllDisplays:', err)
  }
  await sampleCursor()
  draw()
  startFollow()
}

void boot()
