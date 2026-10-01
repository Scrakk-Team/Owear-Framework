// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/browserwindow.ts — BrowserWindow (C9).
import { EventEmitter } from 'node:events'
import { channel, invokeNative } from './channel.js'
import { windowIcon } from './appicon.js'
import { app, readyPromise } from './app.js'
import type { WindowOptions, Bounds } from './index.js'
import { WebContents, WIN_EVENT_NAMES, WC_EVENT_NAMES } from './webcontents.js'
import {
  setCharter,
  getCharter,
  clearCharter,
  type Charter,
  type CharterState,
} from './charter.js'

// ── BrowserWindow ───────────────────────────────────────────────────────────

/** Registro de ventanas por id (para resolver clicks de menú). */
export const windowsById = new Map<number, BrowserWindow>()
/** Ventana con foco (para `BrowserWindow.getFocusedWindow`). */
let focusedWindowId = 0

export class BrowserWindow extends EventEmitter {
  private _id: number | null = null
  private _options: WindowOptions
  private _wc = new WebContents(null)
  private _onWindowEvent: (params: any) => void
  private _onDisconnected: () => void

  constructor(options: WindowOptions = {}) {
    super()
    this._options = options
    if (!readyPromise) {
      throw new Error('BrowserWindow creado antes de app.whenReady()')
    }
    this._onWindowEvent = (params: any) => {
      if (params.windowId !== this._id) return
      this.emit(params.name, params.payload)
      const dashWin = WIN_EVENT_NAMES[params.name]
      if (dashWin) this.emit(dashWin, params.payload)
      this._wc.emit(WC_EVENT_NAMES[params.name] ?? params.name, params.payload)
      if (params.name === 'focus' && this._id != null) focusedWindowId = this._id
      if (params.name === 'blur' && this._id != null && focusedWindowId === this._id)
        focusedWindowId = 0
      if (params.name === 'closed') {
        if (this._id != null) windowsById.delete(this._id)
        this._unwireEvents()
      }
    }
    this._onDisconnected = () => this.emit('disconnected')
    readyPromise.then(() => this._create()).catch((e) => this.emit('error', e))
  }

  private async _create(): Promise<void> {
    // `charter` is applied after the window exists: the kernel needs its id.
    const { icon, charter, ...rest } = this._options
    const res = await channel.call<{ windowId: number }>('window.create', { ...rest })
    this._id = res.windowId
    this._wc._setWindowId(res.windowId)
    windowsById.set(res.windowId, this)
    this._wireEvents()
    if (charter) await this.setCharter(charter).catch((e) => this.emit('error', e))
    // Icono: el de la opción o el global (app.setIcon). PNG/JPEG.
    const ic = icon ?? windowIcon()
    if (ic) await this.setIcon(ic).catch(() => undefined)
    this.emit('ready-to-show', this._id)
  }

  /** Fija el icono de ESTA ventana (ruta a PNG/JPEG). */
  async setIcon(path: string): Promise<void> {
    const { readFileSync } = await import('node:fs')
    const pngB64 = readFileSync(path).toString('base64')
    await invokeNative('window', 'setIcon', this.requireId(), pngB64)
  }

  /**
   * Declares what this window's own document may reach in the kernel. Once a
   * charter is set, every call it does not grant is refused: deny by default.
   */
  async setCharter(charter: Charter): Promise<CharterState> {
    return setCharter(this, charter)
  }

  /** Current charter of this window (`enforce:false` when it has none). */
  getCharter(): Promise<CharterState> {
    return getCharter(this)
  }

  /** Removes the charter: the window goes back to the default (no filtering). */
  clearCharter(): Promise<CharterState> {
    return clearCharter(this)
  }

  private _wireEvents() {
    channel.on('window.event', this._onWindowEvent)
    channel.on('disconnected', this._onDisconnected)
  }

  private _unwireEvents() {
    channel.off('window.event', this._onWindowEvent)
    channel.off('disconnected', this._onDisconnected)
  }

  get id(): number | null {
    return this._id
  }

  private requireId(): number {
    if (this._id == null) throw new Error('ventana aún no creada')
    return this._id
  }

  // ── carga ──────────────────────────────────────────────────────────────
  loadURL(url: string): Promise<void> {
    return channel.call('window.loadURL', { windowId: this.requireId(), url })
  }

  /** Evalúa JS; devuelve el resultado JSON-serializado. */
  eval<T = unknown>(js: string): Promise<T> {
    return channel.call<T>('window.eval', { windowId: this.requireId(), js })
  }

  // ── ciclo de vida ──────────────────────────────────────────────────────
  show(): Promise<void> {
    return channel.call('window.show', { windowId: this.requireId() })
  }
  hide(): Promise<void> {
    return channel.call('window.hide', { windowId: this.requireId() })
  }
  focus(): Promise<void> {
    return channel.call('window.focus', { windowId: this.requireId() })
  }
  close(): Promise<void> {
    return channel.call('window.close', { windowId: this.requireId() })
  }
  destroy(): Promise<void> {
    return channel.call('window.destroy', { windowId: this.requireId() })
  }
  minimize(): Promise<void> {
    return channel.call('window.minimize', { windowId: this.requireId() })
  }
  maximize(): Promise<void> {
    return channel.call('window.maximize', { windowId: this.requireId(), enabled: true })
  }
  unmaximize(): Promise<void> {
    return channel.call('window.maximize', { windowId: this.requireId(), enabled: false })
  }
  setFullScreen(enabled: boolean): Promise<void> {
    return channel.call('window.setFullScreen', { windowId: this.requireId(), enabled })
  }

  /**
   * Responde a un `closeRequested` (F3.4).
   * `allow=false` cancela el cierre; `true` destruye la ventana.
   * Si nadie responde en `OW_CLOSE_TIMEOUT_MS` (default 1000 ms), el kernel
   * cierra igualmente.
   */
  closeRespond(requestId: number, allow: boolean): Promise<void> {
    return channel.call('window.respondCloseRequest', {
      windowId: this.requireId(),
      requestId,
      allow,
    })
  }

  // ── estado ─────────────────────────────────────────────────────────────
  isMaximized(): Promise<boolean> {
    return channel.call('window.isMaximized', { windowId: this.requireId() })
  }
  isMinimized(): Promise<boolean> {
    return channel.call('window.isMinimized', { windowId: this.requireId() })
  }
  getBounds(): Promise<Bounds> {
    return channel.call('window.getBounds', { windowId: this.requireId() })
  }
  setBounds(b: Partial<Bounds>): Promise<void> {
    return channel.call('window.setBounds', { windowId: this.requireId(), ...b })
  }
  setTitle(title: string): Promise<void> {
    return channel.call('window.setTitle', { windowId: this.requireId(), title })
  }

  /**
   * Activa/desactiva o reconfigura los botones nativos de ventana
   * (min/max/close) dentro de la titlebar custom (Linux: GTK `titlebutton`).
   * `true` los activa con los valores por defecto.
   */
  setTitleBarOverlay(
    overlay: boolean | { enabled?: boolean; color?: string; symbolColor?: string; buttonColor?: string; height?: number },
  ): Promise<void> {
    return channel.call('window.setTitleBarOverlay', {
      windowId: this.requireId(),
      titleBarOverlay: overlay,
    })
  }

  // ── estado extendido (C9) ──────────────────────────────────────────────
  isVisible(): Promise<boolean> { return channel.call('window.isVisible', { windowId: this.requireId() }) }
  isFocused(): Promise<boolean> { return channel.call('window.isFocused', { windowId: this.requireId() }) }
  isResizable(): Promise<boolean> { return channel.call('window.isResizable', { windowId: this.requireId() }) }
  isMovable(): Promise<boolean> { return channel.call('window.isMovable', { windowId: this.requireId() }) }
  isMinimizable(): Promise<boolean> { return channel.call('window.isMinimizable', { windowId: this.requireId() }) }
  isMaximizable(): Promise<boolean> { return channel.call('window.isMaximizable', { windowId: this.requireId() }) }
  isClosable(): Promise<boolean> { return channel.call('window.isClosable', { windowId: this.requireId() }) }
  isAlwaysOnTop(): Promise<boolean> { return channel.call('window.isAlwaysOnTop', { windowId: this.requireId() }) }
  isKiosk(): Promise<boolean> { return channel.call('window.isKiosk', { windowId: this.requireId() }) }
  isDestroyed(): Promise<boolean> { return channel.call('window.isDestroyed', { windowId: this.requireId() }) }
  isFullScreen(): Promise<boolean> { return channel.call('window.isFullScreen', { windowId: this.requireId() }) }

  setResizable(on = true): Promise<void> { return channel.call('window.setResizable', { windowId: this.requireId(), on }) }
  setMovable(on = true): Promise<void> { return channel.call('window.setMovable', { windowId: this.requireId(), on }) }
  setMinimizable(on = true): Promise<void> { return channel.call('window.setMinimizable', { windowId: this.requireId(), on }) }
  setMaximizable(on = true): Promise<void> { return channel.call('window.setMaximizable', { windowId: this.requireId(), on }) }
  setClosable(on = true): Promise<void> { return channel.call('window.setClosable', { windowId: this.requireId(), on }) }
  setAlwaysOnTop(on = true, level = 0): Promise<void> { return channel.call('window.setAlwaysOnTop', { windowId: this.requireId(), on, level }) }
  setSkipTaskbar(on = true): Promise<void> { return channel.call('window.setSkipTaskbar', { windowId: this.requireId(), on }) }
  setHasShadow(on = true): Promise<void> { return channel.call('window.setHasShadow', { windowId: this.requireId(), on }) }
  setKiosk(on = true): Promise<void> { return channel.call('window.setKiosk', { windowId: this.requireId(), on }) }
  setIgnoreMouseEvents(ignore = true, options: { forward?: boolean } = {}): Promise<void> {
    return channel.call('window.setIgnoreMouseEvents', { windowId: this.requireId(), ignore, forward: !!options.forward })
  }
  setProgressBar(value: number, options: { mode?: 'none' | 'normal' | 'indeterminate' | 'paused' | 'error' } = {}): Promise<void> {
    return channel.call('window.setProgressBar', { windowId: this.requireId(), value, mode: options.mode ?? 'normal' })
  }
  setBackgroundColor(color: string): Promise<void> {
    return channel.call('window.setBackgroundColor', { windowId: this.requireId(), color })
  }
  moveTop(): Promise<void> { return channel.call('window.moveTop', { windowId: this.requireId() }) }
  setAspectRatio(ratio: number, extraSize?: { width: number; height: number }): Promise<void> {
    return channel.call('window.setAspectRatio', {
      windowId: this.requireId(),
      ratio,
      extraW: extraSize?.width ?? 0,
      extraH: extraSize?.height ?? 0,
    })
  }

  getContentBounds(): Promise<Bounds> { return channel.call('window.getContentBounds', { windowId: this.requireId() }) }
  setContentSize(width: number, height: number): Promise<void> {
    return channel.call('window.setContentSize', { windowId: this.requireId(), width, height })
  }
  getContentSize(): Promise<{ width: number; height: number }> {
    return channel.call('window.getContentSize', { windowId: this.requireId() })
  }
  getMinimumSize(): Promise<{ width: number; height: number }> {
    return channel.call('window.getMinimumSize', { windowId: this.requireId() })
  }
  getMaximumSize(): Promise<{ width: number; height: number }> {
    return channel.call('window.getMaximumSize', { windowId: this.requireId() })
  }
  setMinimumSize(width: number, height: number): Promise<void> {
    return channel.call('window.setMinimumSize', { windowId: this.requireId(), width, height })
  }
  setMaximumSize(width: number, height: number): Promise<void> {
    return channel.call('window.setMaximumSize', { windowId: this.requireId(), width, height })
  }

  // ── estáticos (C9) ─────────────────────────────────────────────────────
  /** Ventanas creadas por esta app (orden de creación). Síncrono, como Electron. */
  static getAllWindows(): BrowserWindow[] {
    return [...windowsById.values()]
  }
  /** Ventana con foco, o `null`. Síncrono, como Electron. */
  static getFocusedWindow(): BrowserWindow | null {
    return focusedWindowId ? windowsById.get(focusedWindowId) ?? null : null
  }
  static fromId(id: number): BrowserWindow | null {
    return windowsById.get(id) ?? null
  }

  /**
   * `webContents` de esta ventana (estilo Electron): `send`, `capturePage`,
   * eventos (`did-finish-load`, `before-input-event`, …) y
   * `setWindowOpenHandler`.
   */
  get webContents(): WebContents {
    return this._wc
  }
}


