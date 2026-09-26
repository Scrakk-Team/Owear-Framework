// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core — SDK JavaScript del kernel Owear.
//
// El kernel nativo expone un Control Socket (UDS / named pipe). Este SDK
// habla NDJSON con él: {"id":N,"cmd":"...","params":{...}}.
//
// Uso (main process, estilo Electron):
//   import { app, BrowserWindow } from '@owear/core'
//   app.whenReady().then(() => {
//     const win = new BrowserWindow({ width: 1200, height: 800 })
//     win.loadURL('http://localhost:5173')
//   })
//

import * as net from 'node:net'
import { EventEmitter } from 'node:events'
import * as os from 'node:os'
import * as path from 'node:path'
import * as fs from 'node:fs'

// ── tipos de la API pública ─────────────────────────────────────────────────

export interface WindowOptions {
  title?: string
  width?: number
  height?: number
  x?: number
  y?: number
  resizable?: boolean
  frameless?: boolean
  titleBarStyle?: 'default' | 'hidden' | 'custom'
  titleBarOverlay?: boolean | { enabled?: boolean; color?: string; symbolColor?: string; buttonColor?: string; height?: number }
  url?: string
}

export interface Bounds {
  x: number
  y: number
  width: number
  height: number
}

export type WindowEventMap = {
  resize: Bounds
  move: Bounds
  focus: void
  blur: void
  maximize: void
  unmaximize: void
  enterFullScreen: void
  leaveFullScreen: void
  closeRequested: { requestId: number }
  closed: void
}

// ── transporte de control ───────────────────────────────────────────────────

type PendingEntry = { resolve: (v: any) => void; reject: (e: Error) => void }

class ControlChannel extends EventEmitter {
  private socket: net.Socket | null = null
  private buffer = ''
  private nextId = 1
  private pending = new Map<number, PendingEntry>()

  connect(socketPath: string): Promise<void> {
    return new Promise((resolve, reject) => {
      const sock = net.connect(socketPath)
      sock.once('connect', () => {
        this.socket = sock
        resolve()
      })
      sock.once('error', (err: Error) => {
        this.socket = null
        this.rejectAllPending(err)
        reject(err)
      })
      sock.on('data', (chunk: Buffer) => {
        this.buffer += chunk.toString('utf8')
        let idx: number
        while ((idx = this.buffer.indexOf('\n')) >= 0) {
          const line = this.buffer.slice(0, idx)
          this.buffer = this.buffer.slice(idx + 1)
          if (line.trim()) this.handleLine(line)
        }
      })
      sock.on('close', () => {
        this.socket = null
        this.rejectAllPending(new Error('conexión cerrada'))
        this.emit('disconnected')
      })
    })
  }

  private rejectAllPending(err: Error) {
    for (const { reject } of this.pending.values()) {
      reject(err)
    }
    this.pending.clear()
  }

  private handleLine(line: string) {
    let msg: any
    try {
      msg = JSON.parse(line)
    } catch {
      return
    }
    if (msg.event) {
      this.emit(msg.event, msg.params)
      return
    }
    const p = this.pending.get(msg.id)
    if (!p) return
    this.pending.delete(msg.id)
    if (msg.ok) p.resolve(msg.result)
    else p.reject(new Error(msg.error ?? 'error de control'))
  }

  call<T = any>(cmd: string, params: Record<string, unknown> = {}): Promise<T> {
    if (!this.socket) return Promise.reject(new Error('control socket desconectado'))
    const id = this.nextId++
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve, reject })
      this.socket!.write(JSON.stringify({ id, cmd, params }) + '\n')
    })
  }
}

// ── app ─────────────────────────────────────────────────────────────────────

const channel = new ControlChannel()

function socketPathFromEnv(): string {
  if (process.env.OW_CONTROL_SOCKET) return process.env.OW_CONTROL_SOCKET
  // fallback: último socket vivo en XDG_RUNTIME_DIR (útil para depurar)
  const dir = process.env.XDG_RUNTIME_DIR
  if (dir) {
    try {
      const socks = fs
        .readdirSync(dir)
        .filter((f) => f.startsWith('owear-') && f.endsWith('.sock'))
        .sort()
      if (socks.length) return path.join(dir, socks[socks.length - 1])
    } catch {
      /* noop */
    }
  }
  throw new Error(
    'Owear: OW_CONTROL_SOCKET no definida. Lanza la app con `ow dev` o `ow build && owear`.'
  )
}

let readyPromise: Promise<void> | null = null

export const app = {
  /** Conecta con el kernel. Resuelve cuando el canal de control está listo. */
  whenReady(): Promise<void> {
    if (!readyPromise) {
      readyPromise = channel.connect(socketPathFromEnv()).then(() => undefined)
    }
    return readyPromise
  },

  quit(exitCode = 0): Promise<void> {
    return channel.call('app.quit', { exitCode }).catch(() => undefined)
  },

  info(): Promise<{ pid: number; version: string; socket: string }> {
    return channel.call('app.info')
  },

  /**
   * Localiza (o descarga) un runtime Node. Prioridad: OW_NODE_BIN → Node del
   * sistema → caché de Owear → descarga oficial. `source` indica de dónde
   * salió ("env" | "system" | "cache" | "downloaded").
   */
  ensureNodeRuntime(
    range = 'latest'
  ): Promise<{ path: string; version: string; source: string }> {
    return channel.call('node.ensure', { range })
  },

  /** Acceso crudo al canal (para módulos custom del SDK). */
  __channel: channel,
}

/**
 * Invoca una función de un módulo nativo del kernel desde el proceso
 * principal (fs, process, net, clipboard…). Es el equivalente de `ow.invoke()`
 * del renderer, pero sin depender de una ventana: es la pieza que permite
 * portar APIs de Electron al main process.
 *
 *   const txt = await invokeNative<string>('fs', 'readText', '/etc/hostname')
 */
export function invokeNative<T = unknown>(
  module: string,
  method: string,
  ...args: unknown[]
): Promise<T> {
  return channel.call<T>('module.invoke', { module, method, args })
}

/** Metadatos de un módulo nativo cargado en el kernel. */
export interface NativeModuleInfo {
  name: string
  version: string
  /** Ruta del .owm o "builtin:<nombre>" para los enlazados al kernel. */
  origin: string
  builtin: boolean
  /** Número de funciones (compat). */
  functions: number
  /** Nombres de las funciones. */
  functionNames: string[]
}

/** Módulos nativos cargados, con versión, origen y funciones. */
export function listNativeModules(): Promise<NativeModuleInfo[]> {
  return channel.call('module.list')
}

/** Metadatos de un módulo nativo concreto. Rechaza si no existe. */
export function nativeModuleInfo(name: string): Promise<NativeModuleInfo> {
  return channel.call('module.info', { name })
}

// ── BrowserWindow ───────────────────────────────────────────────────────────

export class BrowserWindow extends EventEmitter {
  private _id: number | null = null
  private _options: WindowOptions
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
      if (params.name === 'closed') this._unwireEvents()
    }
    this._onDisconnected = () => this.emit('disconnected')
    readyPromise.then(() => this._create()).catch((e) => this.emit('error', e))
  }

  private async _create(): Promise<void> {
    const params: Record<string, unknown> = { ...this._options }
    const res = await channel.call<{ windowId: number }>('window.create', params)
    this._id = res.windowId
    this._wireEvents()
    this.emit('ready-to-show', this._id)
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
}

// ── utilidades del renderer (tipos del bridge inyectado) ────────────────────

/** Tipos de `window.ow` disponible dentro del WebView (inyectado por el kernel). */
export interface OwBridge {
  invoke<T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T>
  on(name: string, cb: (payload: any) => void): () => void
  emit(name: string, payload?: unknown): void
}

declare global {
  interface Window {
    ow?: OwBridge
  }
  // eslint-disable-next-line no-var
  const ow: OwBridge
}

export const platform = os.platform()
