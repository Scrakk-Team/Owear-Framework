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
import { forkWorker, type ForkWorkerOptions, type WorkerHandle } from './node/worker.js'

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

/**
 * Vista mínima de `webContents` (paridad con Electron) para el envío dirigido
 * main → renderer. `send` llega al renderer como `ow.on(name, cb)`.
 */
export interface WebContentsHandle {
  /** Id de la ventana/webContents (-1 si aún no se creó). */
  readonly id: number
  /** Envía un evento SOLO a esta ventana. */
  send(name: string, payload?: unknown): Promise<void>
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

// ── Puente Node (renderer ↔ main) ───────────────────────────────────────────
// El renderer llama `ow.invoke('node','call',{fn,args})`; el kernel lo reenvía
// al main por el control socket; aquí se despacha al handler registrado con
// app.handle y se responde con `node.respond`. Es OPT-IN: solo para features
// que necesitan Node (p. ej. el extension host).
type NodeHandler = (...args: any[]) => unknown | Promise<unknown>
/** Handler con contexto: recibe la ventana de origen y luego los args. */
export type ContextNodeHandler = (
  context: { windowId: number },
  ...args: any[]
) => unknown | Promise<unknown>
const nodeHandlers = new Map<string, NodeHandler>()
const nodeContextHandlers = new Map<string, ContextNodeHandler>()

channel.on('node.request', (params: any) => {
  const { reqId, fn, args, windowId } = params ?? {}
  const handler = nodeHandlers.get(fn)
  const ctxHandler = nodeContextHandlers.get(fn)
  const argsArr = Array.isArray(args) ? args : args === undefined ? [] : [args]
  Promise.resolve()
    .then(() =>
      ctxHandler
        ? ctxHandler({ windowId: typeof windowId === 'number' ? windowId : 0 }, ...argsArr)
        : handler
          ? handler(...argsArr)
          : undefined
    )
    .then(
      (result) => channel.call('node.respond', { reqId, ok: true, result }),
      (err) =>
        channel.call('node.respond', {
          reqId,
          ok: false,
          result: { message: err instanceof Error ? err.message : String(err) },
        })
    )
    .catch(() => undefined)
})

// ── MessagePort (canal bidireccional main ↔ renderer) ───────────────────────
// Cada endpoint vive o en el main o en un renderer. Los mensajes se enrutan por
// el bridge. El payload viaja como JSON (igual que el resto del bridge); el
// transporte binario sin copia kernel→renderer sigue siendo `ow-shm://`.
export interface MessagePortMain {
  /** Id global del endpoint (para transferirlo a un renderer). */
  readonly portId: number
  /** Envía un mensaje al otro extremo. */
  postMessage(message: unknown): void
  on(event: 'message', listener: (message: unknown) => void): this
  start(): void
  close(): void
}

type PortSide = { kind: 'main' } | { kind: 'renderer'; windowId: number }
interface PortRecord {
  id: number
  peer: number
  side: PortSide
  emitter: EventEmitter
}

const portRecords = new Map<number, PortRecord>()
let nextPortId = 1

function deliverPort(fromId: number, data: unknown): void {
  const rec = portRecords.get(fromId)
  if (!rec) return
  const peer = portRecords.get(rec.peer)
  if (!peer) return
  if (peer.side.kind === 'main') {
    peer.emitter.emit('message', data)
  } else {
    void channel.call('node.emit', {
      name: '__ow_port:msg',
      payload: { id: peer.id, data },
      windowId: peer.side.windowId,
    })
  }
}

function makeMainPort(id: number): MessagePortMain {
  const rec = portRecords.get(id)!
  return Object.assign(rec.emitter, {
    portId: id,
    postMessage: (data: unknown) => deliverPort(id, data),
    start: () => undefined,
    close: () => portRecords.delete(id),
  }) as unknown as MessagePortMain
}

// Handler reservado: el renderer publica en un puerto con
// `ow.invoke('node','call',{ fn:'__ow_port_post', args:[{ id, data }] })`.
nodeHandlers.set('__ow_port_post', (msg: { id: number; data: unknown }) => {
  if (msg && typeof msg.id === 'number') deliverPort(msg.id, msg.data)
  return null
})

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

  /**
   * Registra un handler invocable desde el renderer con
   * `ow.invoke('node', 'call', { fn, args })`. Devuelve un unsubscribe.
   * Es la vía para exponer Node a la UI (extension host, libs Node-only).
   */
  handle(fn: string, handler: NodeHandler): () => void {
    nodeHandlers.set(fn, handler)
    return () => {
      if (nodeHandlers.get(fn) === handler) nodeHandlers.delete(fn)
    }
  },

  /**
   * Como `handle`, pero el handler recibe la ventana de origen:
   *   app.handleContext('x', (ctx, payload) => ctx.windowId)
   * `ctx.windowId` es 0 si la llamada no vino de una ventana concreta.
   */
  handleContext(fn: string, handler: ContextNodeHandler): () => void {
    nodeContextHandlers.set(fn, handler)
    return () => {
      if (nodeContextHandlers.get(fn) === handler) nodeContextHandlers.delete(fn)
    }
  },

  /**
   * Empuja un evento a los renderers, recibible con `ow.on(name, cb)`.
   * `windowId` opcional para dirigirlo a una ventana concreta.
   */
  send(name: string, payload?: unknown, windowId?: number): Promise<void> {
    return channel.call('node.emit', { name, payload, windowId })
  },

  /**
   * Lanza un worker Node con canal de mensajes (reemplazo de
   * `utilityProcess.fork`). El entry puede ser absoluto, relativo a
   * `app.workersDir()`, o con `./` relativo al cwd.
   *
   *   const w = app.forkWorker('tree-sitter-worker.js')
   *   w.postMessage({ op: 'tokenize', text })
   *   w.on('message', (m) => …)
   */
  forkWorker(entry: string, options?: ForkWorkerOptions): WorkerHandle {
    return forkWorker(entry, options)
  },

  /** Directorio de workers compilados (`OW_APP_WORKERS`), si `ow dev`/`ow build` lo definió. */
  workersDir(): string | undefined {
    return process.env.OW_APP_WORKERS
  },

  /**
   * Crea un canal bidireccional de dos puertos. Ambos nacen en el main;
   * transfiere uno a un renderer con `app.sendPort(windowId, name, port)`.
   * El renderer lo recibe con `ow.on(name, ({ port }) => ow.port(port))`.
   */
  createChannel(): { port1: MessagePortMain; port2: MessagePortMain } {
    const a = nextPortId++
    const b = nextPortId++
    portRecords.set(a, { id: a, peer: b, side: { kind: 'main' }, emitter: new EventEmitter() })
    portRecords.set(b, { id: b, peer: a, side: { kind: 'main' }, emitter: new EventEmitter() })
    return { port1: makeMainPort(a), port2: makeMainPort(b) }
  },

  /**
   * Transfiere un puerto a una ventana. El renderer lo recibe como
   * `ow.on(name, ({ port }) => { const p = ow.port(port); … })`.
   */
  sendPort(windowId: number, name: string, port: MessagePortMain): Promise<void> {
    const rec = portRecords.get(port.portId)
    if (rec) rec.side = { kind: 'renderer', windowId }
    return channel.call('node.emit', {
      name,
      payload: { port: port.portId },
      windowId,
    })
  },
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

  /**
   * Vista de `webContents` de esta ventana (paridad Electron): envío dirigido
   * main → renderer SOLO a esta ventana. El renderer lo recibe con
   * `ow.on(name, cb)`.
   */
  get webContents(): WebContentsHandle {
    const id = this._id
    return {
      get id() {
        return id ?? -1
      },
      send: (name: string, payload?: unknown) =>
        id == null
          ? Promise.reject(new Error('ventana aún no creada'))
          : channel.call('node.emit', { name, payload, windowId: id }),
    }
  }
}

// ── workers Node (utilityProcess.fork) ──────────────────────────────────────

export { forkWorker, resolveWorkerEntry } from './node/worker.js'
export type { ForkWorkerOptions, WorkerHandle } from './node/worker.js'

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
