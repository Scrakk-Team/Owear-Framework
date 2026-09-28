// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/screen.ts — pantallas (multi-monitor) y eventos.
import { EventEmitter } from 'node:events'
import { invokeNative } from './channel.js'
import { app } from './index.js'

// ── screen (C10) ────────────────────────────────────────────────────────────
// Nombres intermedios Owear: métodos descriptivos + eventos cortos
// (`added`/`removed`/`changed`). Los alias estilo Electron se ofrecen para
// portar apps existentes.
export interface DisplayBounds {
  x: number
  y: number
  width: number
  height: number
}
export interface DisplaySize {
  width: number
  height: number
}
export interface Display {
  id: number
  primary: boolean
  /** Etiqueta legible (modelo/monitor); puede venir vacía. */
  label: string
  /** En píxeles NATIVOS de pantalla (coincide con las coords del kernel). */
  bounds: DisplayBounds
  size: DisplaySize
  workArea: DisplayBounds
  workAreaSize: DisplaySize
  scaleFactor: number
  rotation: number
  internal: boolean
  detected: boolean
  displayFrequency: number
  colorDepth: number
  depthPerComponent: number
  colorSpace: string
  monochrome: boolean
  touchSupport: 'available' | 'unavailable' | 'unknown'
  accelerometerSupport: 'available' | 'unavailable' | 'unknown'
  nativeOrigin: { x: number; y: number }
}
export type DisplayMetric = 'bounds' | 'workArea' | 'scaleFactor' | 'rotation'

function distanceToRect(p: { x: number; y: number }, r: DisplayBounds): number {
  const dx = Math.max(r.x - p.x, 0, p.x - (r.x + r.width))
  const dy = Math.max(r.y - p.y, 0, p.y - (r.y + r.height))
  return dx * dx + dy * dy
}
function intersectionArea(a: DisplayBounds, b: DisplayBounds): number {
  const w = Math.min(a.x + a.width, b.x + b.width) - Math.max(a.x, b.x)
  const h = Math.min(a.y + a.height, b.y + b.height) - Math.max(a.y, b.y)
  return w > 0 && h > 0 ? w * h : 0
}

class Screen extends EventEmitter {
  private _watching = false

  private async _ensure(): Promise<void> {
    if (this._watching) return
    this._watching = true
    try {
      await app.whenReady()
      await invokeNative<void>('screen', 'watch')
    } catch {
      /* sin kernel: módulo no disponible */
    }
  }

  /** Conecta el watcher nativo (se llama solo al usar el módulo o un evento). */
  watch(): Promise<void> {
    return this._ensure()
  }

  on(event: string, listener: (...args: any[]) => void): this {
    void this._ensure()
    return super.on(event, listener)
  }
  once(event: string, listener: (...args: any[]) => void): this {
    void this._ensure()
    return super.once(event, listener)
  }

  async getAllDisplays(): Promise<Display[]> {
    await this._ensure()
    return invokeNative<Display[]>('screen', 'getAllDisplays')
  }
  async getPrimaryDisplay(): Promise<Display | null> {
    await this._ensure()
    return invokeNative<Display | null>('screen', 'getPrimaryDisplay')
  }
  async getCursorScreenPoint(): Promise<{ x: number; y: number }> {
    return invokeNative<{ x: number; y: number }>('screen', 'getCursorScreenPoint')
  }

  /** Display más cercano a un punto (calculado en el SDK sobre los bounds). */
  async getDisplayNearestPoint(point: { x: number; y: number }): Promise<Display | null> {
    const all = await this.getAllDisplays()
    let best: Display | null = null
    let bestD = Infinity
    for (const d of all) {
      const dist = distanceToRect(point, d.bounds)
      if (dist < bestD) {
        bestD = dist
        best = d
      }
    }
    return best
  }

  /** Display que más se solapa con un rect (o el más cercano a su centro). */
  async getDisplayMatching(rect: DisplayBounds): Promise<Display | null> {
    const all = await this.getAllDisplays()
    let best: Display | null = null
    let bestA = 0
    for (const d of all) {
      const area = intersectionArea(rect, d.bounds)
      if (area > bestA) {
        bestA = area
        best = d
      }
    }
    if (best) return best
    const center = { x: rect.x + rect.width / 2, y: rect.y + rect.height / 2 }
    return this.getDisplayNearestPoint(center)
  }

  private async _displayAt(point: { x: number; y: number }): Promise<Display | null> {
    return this.getDisplayNearestPoint(point)
  }

  /**
   * Punto físico de pantalla → DIP, relativo al display que lo contiene
   * (aproximación por escala del display; coincide exactamente con escala 1).
   */
  async screenToDipPoint(point: { x: number; y: number }): Promise<{ x: number; y: number }> {
    const d = await this._displayAt(point)
    const s = d?.scaleFactor ?? 1
    if (!d || s <= 1) return point
    return {
      x: d.bounds.x + Math.round((point.x - d.bounds.x) / s),
      y: d.bounds.y + Math.round((point.y - d.bounds.y) / s),
    }
  }
  async dipToScreenPoint(point: { x: number; y: number }): Promise<{ x: number; y: number }> {
    const d = await this._displayAt(point)
    const s = d?.scaleFactor ?? 1
    if (!d || s <= 1) return point
    return {
      x: d.bounds.x + Math.round((point.x - d.bounds.x) * s),
      y: d.bounds.y + Math.round((point.y - d.bounds.y) * s),
    }
  }

  // ── alias estilo Electron (portabilidad) ─────────────────────────────────
  getDisplays = this.getAllDisplays.bind(this)
  getPrimary = this.getPrimaryDisplay.bind(this)
  getCursor = this.getCursorScreenPoint.bind(this)
  nearestDisplay = this.getDisplayNearestPoint.bind(this)
  displayMatching = this.getDisplayMatching.bind(this)
}

/** Pantallas del sistema + eventos de cambio (multi-monitor). */
export const screen = new Screen()


