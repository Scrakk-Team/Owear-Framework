// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/power.ts — powerMonitor / powerSaveBlocker.
import { EventEmitter } from 'node:events'
import { invokeNative } from './channel.js'
import { app } from './index.js'

// ── powerMonitor (C10) ──────────────────────────────────────────────────────
export type IdleState = 'active' | 'idle' | 'locked' | 'unknown'

class PowerMonitor extends EventEmitter {
  private _watching = false
  private _onBattery = false

  private async _ensure(): Promise<void> {
    if (this._watching) return
    this._watching = true
    try {
      await app.whenReady()
      await invokeNative<void>('power', 'monitorStart')
      this._onBattery = await invokeNative<boolean>('power', 'isOnBattery')
    } catch {
      /* sin kernel: módulo no disponible */
    }
  }

  /** Empieza a escuchar eventos de energía (auto al usar el módulo o un evento). */
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

  getIdleTime(): Promise<number> {
    return invokeNative<number>('power', 'idleTime')
  }
  getIdleState(threshold: number): Promise<IdleState> {
    return invokeNative<IdleState>('power', 'idleState', threshold)
  }
  isOnBatteryPower(): Promise<boolean> {
    return invokeNative<boolean>('power', 'isOnBattery').then((v) => {
      this._onBattery = v
      return v
    })
  }
  /** Último valor conocido (se actualiza con los eventos ac/battery). */
  get onBatteryPower(): boolean {
    return this._onBattery
  }

  /** @internal */
  _setOnBattery(v: boolean): void {
    this._onBattery = v
  }

  getSystemIdleTime = this.getIdleTime.bind(this)
  getSystemIdleState = this.getIdleState.bind(this)
}

/** Eventos de energía: `suspend`, `resume`, `ac`, `battery`, `shutdown`, `lock`, `unlock`. */
export const powerMonitor = new PowerMonitor()

// ── powerSaveBlocker (C10) ──────────────────────────────────────────────────
export type PowerSaveBlockerType = 'prevent-app-suspension' | 'prevent-display-sleep'

interface BlockerRecord {
  type: PowerSaveBlockerType
  native: number | null
}
const activeBlockers = new Map<number, BlockerRecord>()
let nextBlockerId = 1

/**
 * Evita que el sistema entre en suspensión. `prevent-display-sleep` tiene
 * precedencia sobre `prevent-app-suspension`.
 */
export const powerSaveBlocker = {
  start(type: PowerSaveBlockerType = 'prevent-display-sleep'): number {
    const id = nextBlockerId++
    activeBlockers.set(id, { type, native: null })
    void (async () => {
      try {
        await app.whenReady()
        const native = await invokeNative<number>('power', 'inhibitStart', type)
        const b = activeBlockers.get(id)
        if (b) b.native = native
        else await invokeNative('power', 'inhibitStop', native)
      } catch {
        /* sin kernel: el bloqueador queda solo en el registro JS */
      }
    })()
    return id
  },
  stop(id: number): boolean {
    const b = activeBlockers.get(id)
    if (!b) return false
    activeBlockers.delete(id)
    if (b.native != null)
      void invokeNative('power', 'inhibitStop', b.native).catch(() => undefined)
    return true
  },
  isStarted(id: number): boolean {
    return activeBlockers.has(id)
  },
}



