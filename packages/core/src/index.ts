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
import { NativeImage, nativeImage } from './nativeimage.js'

export { NativeImage, nativeImage } from './nativeimage.js'
export type { Size as NativeImageSize, Rectangle, ResizeOptions } from './nativeimage.js'

// ── tipos de la API pública ─────────────────────────────────────────────────

export interface WindowOptions {
  title?: string
  width?: number
  height?: number
  x?: number
  y?: number
  minWidth?: number
  minHeight?: number
  maxWidth?: number
  maxHeight?: number
  resizable?: boolean
  movable?: boolean
  minimizable?: boolean
  maximizable?: boolean
  closable?: boolean
  fullscreenable?: boolean
  frameless?: boolean
  transparent?: boolean
  backgroundColor?: string
  show?: boolean
  skipTaskbar?: boolean
  alwaysOnTop?: boolean
  hasShadow?: boolean
  aspectRatio?: number
  /** Ventana padre (id de otra ventana) y si es modal respecto a ella. */
  parent?: number
  modal?: boolean
  titleBarStyle?: 'default' | 'hidden' | 'custom'
  titleBarOverlay?: boolean | { enabled?: boolean; color?: string; symbolColor?: string; buttonColor?: string; height?: number }
  url?: string
  /**
   * Partición de sesión: aísla cookies/localStorage/IndexedDB/cache por perfil.
   * Vacío = perfil por defecto de la app. Ej: "persist:cuenta-2".
   * Crea la partición con `session.fromPartition(name)`.
   */
  session?: string
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

// ── protocol (esquemas personalizados) ──────────────────────────────────────
export interface ProtocolRequest {
  url: string
  method: string
  headers: Record<string, string>
  /** Cuerpo de la request en base64 (formato del bridge). */
  body: string
}

export type ProtocolHandlerResult =
  | Response
  | { status?: number; headers?: Record<string, string>; body?: string | Uint8Array }
  | string
  | null
  | undefined

export type ProtocolHandler = (
  req: ProtocolRequest
) => ProtocolHandlerResult | Promise<ProtocolHandlerResult>

const protocolHandlers = new Map<string, ProtocolHandler>()

async function normalizeProtocolResponse(
  res: ProtocolHandlerResult
): Promise<{ status: number; headers: Record<string, string>; body: string }> {
  if (res == null) return { status: 404, headers: {}, body: '' }
  if (typeof res === 'string')
    return { status: 200, headers: { 'content-type': 'text/html' }, body: toB64(res) }
  if (typeof Response !== 'undefined' && res instanceof Response) {
    const headers: Record<string, string> = {}
    res.headers.forEach((v, k) => {
      headers[k] = v
    })
    const body = Buffer.from(await res.arrayBuffer()).toString('base64')
    return { status: res.status, headers, body }
  }
  const r = res as { status?: number; headers?: Record<string, string>; body?: string | Uint8Array }
  let body = ''
  if (r.body instanceof Uint8Array) body = Buffer.from(r.body).toString('base64')
  else if (typeof r.body === 'string') body = toB64(r.body)
  return { status: r.status ?? 200, headers: r.headers ?? {}, body }
}

function toB64(text: string): string {
  return Buffer.from(text, 'utf-8').toString('base64')
}

channel.on('protocol.request', (params: any) => {
  const { reqId, scheme, url, method, headers, body } = params ?? {}
  const handler = protocolHandlers.get(scheme)
  Promise.resolve()
    .then(() =>
      handler
        ? handler({ url, method, headers: headers ?? {}, body: body ?? '' })
        : null
    )
    .then((res) => normalizeProtocolResponse(res ?? null))
    .then((out) =>
      channel.call('protocol.respond', {
        reqId,
        status: out.status,
        headers: out.headers,
        body: out.body,
      })
    )
    .catch(() =>
      channel.call('protocol.respond', { reqId, status: 500, headers: {}, body: '' })
    )
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

// ── app: rutas, identidad, commandLine y eventos de ciclo de vida ──────────
const appEmitter = new EventEmitter()
let appNameOverride: string | null = null
let appInfoCache:
  | { name: string; version: string; appPath: string; exePath: string; packaged: boolean }
  | null = null
const pathOverrides = new Map<string, string>()

channel.on('app.event', (params: any) => {
  appEmitter.emit(params?.name, params?.payload)
})

function readPackageJson(): { name?: string; version?: string } {
  const dirs = [
    process.cwd(),
    process.env.OW_APP_MAIN ? path.dirname(process.env.OW_APP_MAIN) : '',
    process.env.OW_ASSETS_DIR ?? '',
  ]
  for (const dir of dirs) {
    if (!dir) continue
    try {
      return JSON.parse(fs.readFileSync(path.join(dir, 'package.json'), 'utf8'))
    } catch {
      /* siguiente */
    }
  }
  return {}
}
const appPkg = readPackageJson()

function appId(): string {
  return process.env.OW_APP_ID || process.env.OW_APP_NAME || appPkg.name || 'app'
}

/** Rutas síncronas estilo Electron (mismas convenciones que el módulo `path`). */
function computePath(name: string): string {
  const home = os.homedir()
  const win = process.platform === 'win32'
  const appData = win
    ? process.env.APPDATA ?? path.join(home, 'AppData', 'Roaming')
    : process.env.XDG_DATA_HOME ?? path.join(home, '.local', 'share')
  const localData = win
    ? process.env.LOCALAPPDATA ?? path.join(home, 'AppData', 'Local')
    : process.env.XDG_DATA_HOME ?? path.join(home, '.local', 'share')
  switch (name) {
    case 'home':
      return home
    case 'appData':
      return appData
    case 'userData':
      return win ? path.join(localData, appId()) : path.join(process.env.XDG_CONFIG_HOME ?? path.join(home, '.config'), appId())
    case 'sessionData':
      return appInfoCache?.appPath ? path.join(appInfoCache.appPath, 'session') : path.join(localData, appId(), 'session')
    case 'cache':
      return win ? path.join(localData, 'owear', 'cache') : process.env.XDG_CACHE_HOME ?? path.join(home, '.cache')
    case 'temp':
      return os.tmpdir()
    case 'logs':
      return path.join(computePath('userData'), 'logs')
    case 'downloads':
      return win ? path.join(home, 'Downloads') : (process.env.XDG_DOWNLOAD_DIR ?? path.join(home, 'Downloads'))
    case 'documents':
      return path.join(home, 'Documents')
    case 'desktop':
      return path.join(home, 'Desktop')
    case 'pictures':
      return path.join(home, 'Pictures')
    case 'music':
      return path.join(home, 'Music')
    case 'videos':
      return path.join(home, 'Videos')
    case 'exe':
      return appInfoCache?.exePath ?? path.dirname(process.execPath)
    case 'appPath':
      return appInfoCache?.appPath ?? process.cwd()
    default:
      return ''
  }
}

export const app = {
  /** Conecta con el kernel. Resuelve cuando el canal de control está listo. */
  whenReady(): Promise<void> {
    if (!readyPromise) {
      readyPromise = channel.connect(socketPathFromEnv()).then(async () => {
        try {
          appInfoCache = await channel.call('app.info')
        } catch {
          /* sin kernel: rutas locales */
        }
      })
    }
    return readyPromise
  },

  quit(exitCode = 0): Promise<void> {
    return channel.call('app.quit', { exitCode }).catch(() => undefined)
  },

  info(): Promise<{ pid: number; version: string; socket: string }> {
    return channel.call('app.info')
  },

  // ── rutas / identidad (estilo Electron) ────────────────────────────────
  /** Ruta estándar del sistema (home, userData, temp, logs, downloads, exe…). */
  getPath(name: string): string {
    return pathOverrides.get(name) ?? computePath(name)
  },
  setPath(name: string, value: string): void {
    pathOverrides.set(name, value)
  },
  getName(): string {
    return appNameOverride ?? appInfoCache?.name ?? process.env.OW_APP_NAME ?? appPkg.name ?? 'Owear App'
  },
  setName(name: string): void {
    appNameOverride = name
    void channel.call('app.setName', { name }).catch(() => undefined)
  },
  /** Versión de la app (package.json); si no, la del kernel. */
  getVersion(): string {
    return appPkg.version ?? appInfoCache?.version ?? '0.0.0'
  },
  isPackaged(): boolean {
    return !!appInfoCache?.packaged
  },
  getAppPath(): string {
    return appInfoCache?.appPath ?? process.cwd()
  },

  /**
   * Opciones de línea de comandos del WebView. `appendSwitch` se aplica al
   * crear la ventana (Windows: AdditionalBrowserArguments; Linux: best-effort).
   */
  commandLine: {
    _switches: new Map<string, string | undefined>(),
    appendSwitch(key: string, value?: string): void {
      this._switches.set(key, value)
      void channel.call('app.commandLine.appendSwitch', { key, value }).catch(() => undefined)
    },
    appendArgument(arg: string): void {
      void channel.call('app.commandLine.appendArgument', { arg }).catch(() => undefined)
    },
    getSwitchValue(key: string): string {
      return this._switches.get(key) ?? ''
    },
    hasSwitch(key: string): boolean {
      return this._switches.has(key)
    },
  },

  // ── eventos de ciclo de vida (EventEmitter) ────────────────────────────
  on(event: 'before-quit' | 'will-quit' | 'window-all-closed' | 'second-instance' | 'activate' | 'child-process-gone' | string, listener: (...args: any[]) => void): void {
    appEmitter.on(event, listener)
  },
  off(event: string, listener: (...args: any[]) => void): void {
    appEmitter.off(event, listener)
  },
  once(event: string, listener: (...args: any[]) => void): void {
    appEmitter.once(event, listener)
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

  /**
   * Registra un esquema personalizado (protocol API, estilo intermedio):
   *   app.protocol('scrakk-ext', { privileged: { secure: true, cors: true },
   *     handler: async (req) => new Response('<h1>hola</h1>', { headers: {...} }) })
   *   app.protocol('assets', { serve: '/ruta/al/dir' })  // el kernel sirve el dir
   * El handler corre en el main (Node); puede devolver un `Response`, un objeto
   * `{ status, headers, body }`, un string o `null`.
   */
  protocol(
    name: string,
    options: {
      privileged?: {
        secure?: boolean
        cors?: boolean
        stream?: boolean
        standard?: boolean
        fetch?: boolean
      }
      serve?: string
      handler?: ProtocolHandler
    } = {}
  ): Promise<void> {
    protocolHandlers.set(name, options.handler as ProtocolHandler)
    return channel.call('protocol.register', {
      scheme: name,
      privileged: options.privileged,
      dir: options.serve,
      handler: typeof options.handler === 'function',
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

// ── nativeTheme ─────────────────────────────────────────────────────────────

export interface ThemeInfo {
  dark: boolean
  source: 'system' | 'light' | 'dark'
  highContrast: boolean
  reducedTransparency: boolean
}

/**
 * Tema nativo del sistema (módulo `theme`). `watch()` emite `theme.changed`
 * (recibible con `channel.on('theme.changed', …)` o en el renderer con
 * `ow.on('theme.changed', …)`).
 */
export const theme = {
  get(): Promise<ThemeInfo> {
    return invokeNative<ThemeInfo>('theme', 'get')
  },
  isDark(): Promise<boolean> {
    return invokeNative<boolean>('theme', 'isDark')
  },
  /** Fuerza la preferencia: 'system' | 'light' | 'dark'. También aplica el
   *  esquema al contenido (Windows: WebView2 `PreferredColorScheme`). */
  async setSource(source: 'system' | 'light' | 'dark'): Promise<ThemeInfo> {
    const info = await invokeNative<ThemeInfo>('theme', 'setSource', source)
    const scheme = source === 'dark' ? 2 : source === 'light' ? 1 : 0
    void channel.call('window.setColorScheme', { scheme }).catch(() => undefined)
    return info
  },
  watch(): Promise<void> {
    return invokeNative<void>('theme', 'watch')
  },
  unwatch(): Promise<void> {
    return invokeNative<void>('theme', 'unwatch')
  },
}

/** Alias estilo Electron de `theme`. */
export const nativeTheme = theme

// ── safeStorage ─────────────────────────────────────────────────────────────

export interface SafeStorageResult {
  /** El dato cifrado (o el texto tal cual si no hay backend). */
  data: string
  /** true si se cifró de verdad (DPAPI en Windows, AES-GCM en Linux). */
  encrypted: boolean
}

/**
 * Almacenamiento cifrado por el SO (módulo `safestorage`).
 * Windows: DPAPI · Linux: AES-256-GCM con clave local (0600).
 * `decrypt` acepta tanto blobs cifrados como texto plano.
 */
export const safeStorage = {
  isAvailable(): Promise<boolean> {
    return invokeNative<boolean>('safestorage', 'isAvailable')
  },
  encrypt(text: string): Promise<SafeStorageResult> {
    return invokeNative<SafeStorageResult>('safestorage', 'encrypt', text)
  },
  decrypt(data: string): Promise<string> {
    return invokeNative<string>('safestorage', 'decrypt', data)
  },
}

// ── session (permisos del WebView) ──────────────────────────────────────────

export interface PermissionRequest {
  id: number
  /** geolocation | notifications | media | microphone | camera | clipboardRead | … */
  permission: string
  origin: string
}

export type PermissionHandler = (req: PermissionRequest) => boolean | Promise<boolean>

const permissionHandlers: PermissionHandler[] = []

channel.on('session.permissionRequest', (params: any) => {
  const { id, permission, origin } = params ?? {}
  Promise.all(
    permissionHandlers.map((h) => Promise.resolve(h({ id, permission, origin })))
  )
    .then((results) =>
      channel.call('session.respondPermission', { id, allow: results.some(Boolean) })
    )
    .catch(() =>
      channel.call('session.respondPermission', { id, allow: false })
    )
})

/**
 * Permisos del WebView (geolocalización, notificaciones, cámara, micrófono…).
 * El handler decide allow/deny; si no hay handler registrado, se deniega.
 *   session.onPermissionRequest(({ permission, origin }) => permission === 'notifications')
 */
export const session = {
  /**
   * Referencia a una partición de sesión. Pásala al crear la ventana:
   *   const s = session.fromPartition('persist:cuenta-2')
   *   new BrowserWindow({ session: s.partition, url })
   * Aísla cookies/storage/cache por perfil (WebKit: data dir; WebView2: perfil).
   */
  fromPartition(partition: string): { partition: string } {
    return { partition }
  },

  onPermissionRequest(handler: PermissionHandler): () => void {
    permissionHandlers.push(handler)
    // El handler se registra ya; avisar al kernel en cuanto haya canal.
    app
      .whenReady()
      .then(() => channel.call('session.setPermissionHandler', { enabled: true }))
      .catch(() => undefined)
    return () => {
      const i = permissionHandlers.indexOf(handler)
      if (i >= 0) permissionHandlers.splice(i, 1)
    }
  },
}

// ── webRequest (intercepción de requests) ───────────────────────────────────

export interface WebRequestDetails {
  id: number
  url: string
  method: string
  headers: Record<string, string>
}

export type WebRequestResult = { cancel?: boolean; redirectURL?: string } | void
export type WebRequestHandler = (
  details: WebRequestDetails
) => WebRequestResult | Promise<WebRequestResult>

interface WebRequestRegistration {
  patterns: string[]
  handler: WebRequestHandler
}

const webRequestRegistrations: WebRequestRegistration[] = []

function globMatch(pattern: string, url: string): boolean {
  if (pattern === '<all_urls>' || pattern === '*') return true
  const re =
    '^' +
    pattern
      .replace(/[.+^${}()|[\]\\]/g, '\\$&')
      .replace(/\*/g, '.*')
      .replace(/\?/g, '.') +
    '$'
  try {
    return new RegExp(re).test(url)
  } catch {
    return false
  }
}

function webRequestPatterns(): string[] {
  const set = new Set<string>()
  for (const r of webRequestRegistrations) for (const p of r.patterns) set.add(p)
  return [...set]
}

function syncWebRequest(): void {
  app
    .whenReady()
    .then(() => channel.call('webRequest.register', { urls: webRequestPatterns() }))
    .catch(() => undefined)
}

channel.on('webRequest.request', (params: any) => {
  const { id, url, method, headers } = params ?? {}
  const matched = webRequestRegistrations.filter((r) =>
    r.patterns.some((p) => globMatch(p, url))
  )
  Promise.resolve()
    .then(async () => {
      for (const r of matched) {
        const res = await r.handler({ id, url, method, headers: headers ?? {} })
        if (res && (res.cancel || res.redirectURL)) return res
      }
      return null
    })
    .then((res) =>
      channel.call('webRequest.respond', {
        id,
        cancel: !!(res && res.cancel),
        redirectURL: res && res.redirectURL,
      })
    )
    .catch(() => channel.call('webRequest.respond', { id, cancel: false }))
})

/**
 * Intercepción de requests (webRequest). `onBeforeRequest` puede cancelar o
 * redirigir. En **Windows** intercepta todos los requests; en **Linux** solo
 * navegaciones (WebKitGTK 2.52 ya no expone `send-request` para subrecursos).
 *
 *   webRequest.onBeforeRequest({ urls: ['https://ads.example.com/w'] }, () => ({ cancel: true }))
 */
export const webRequest = {
  onBeforeRequest(
    filter: { urls?: string[] } | string[],
    handler: WebRequestHandler
  ): () => void {
    const patterns = Array.isArray(filter) ? filter : filter?.urls ?? ['<all_urls>']
    const reg: WebRequestRegistration = { patterns, handler }
    webRequestRegistrations.push(reg)
    syncWebRequest()
    return () => {
      const i = webRequestRegistrations.indexOf(reg)
      if (i >= 0) webRequestRegistrations.splice(i, 1)
      syncWebRequest()
    }
  },
}

// ── dialog ──────────────────────────────────────────────────────────────────

export interface FileFilter {
  name: string
  extensions: string[]
}

export interface OpenDialogOptions {
  title?: string
  defaultPath?: string
  buttonLabel?: string
  filters?: FileFilter[]
  properties?: Array<
    'openFile' | 'openDirectory' | 'multiSelections' | 'showHiddenFiles' | 'createDirectory'
  >
}

export interface SaveDialogOptions {
  title?: string
  defaultPath?: string
  buttonLabel?: string
  filters?: FileFilter[]
}

export interface MessageBoxOptions {
  type?: 'none' | 'info' | 'error' | 'question' | 'warning'
  title?: string
  message: string
  detail?: string
  buttons?: string[]
  defaultId?: number
  cancelId?: number
  checkboxLabel?: string
}

/**
 * Diálogos nativos (módulo `dialog`).
 *   const { canceled, filePaths } = await dialog.showOpenDialog({ properties: ['openFile','multiSelections'] })
 *   const { filePath } = await dialog.showSaveDialog({ defaultPath: '/tmp/x.txt' })
 *   const { response } = await dialog.showMessageBox({ message: '¿Seguir?', buttons: ['Sí','No'] })
 */
export const dialog = {
  showOpenDialog(options: OpenDialogOptions = {}): Promise<{ canceled: boolean; filePaths: string[] }> {
    return invokeNative('dialog', 'showOpenDialog', options)
  },
  showSaveDialog(options: SaveDialogOptions = {}): Promise<{ canceled: boolean; filePath: string }> {
    return invokeNative('dialog', 'showSaveDialog', options)
  },
  showMessageBox(options: MessageBoxOptions): Promise<{ response: number; checkboxChecked: boolean }> {
    return invokeNative('dialog', 'showMessageBox', options)
  },
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

// ── webContents (C3) ────────────────────────────────────────────────────────

export interface WindowOpenDetails {
  url: string
}
export type WindowOpenHandler = (
  details: WindowOpenDetails
) =>
  | { action: 'allow' | 'deny'; overrideBrowserWindowOptions?: Record<string, unknown> }
  | void

/** Eventos de ventana → nombres estilo Electron de webContents. */
const WC_EVENT_NAMES: Record<string, string> = {
  didFinishLoad: 'did-finish-load',
  didFailLoad: 'did-fail-load',
  navigationStarted: 'did-start-navigation',
  loadCommitted: 'did-navigate',
  pageTitleUpdated: 'page-title-updated',
  beforeInput: 'before-input-event',
}

/** Eventos de ventana → nombres estilo Electron (dash) que emite BrowserWindow. */
const WIN_EVENT_NAMES: Record<string, string> = {
  pageTitleUpdated: 'page-title-updated',
  enterFullScreen: 'enter-full-screen',
  leaveFullScreen: 'leave-full-screen',
  alwaysOnTopChanged: 'always-on-top-changed',
}

let windowOpenHandler: WindowOpenHandler | null = null

channel.on('webContents.windowOpen', (params: any) => {
  const { id, url } = params ?? {}
  let action: 'allow' | 'deny' = 'allow'
  try {
    const r = windowOpenHandler?.({ url })
    if (r && r.action === 'deny') action = 'deny'
  } catch {
    /* handler lanzó → allow */
  }
  void channel.call('webContents.respondWindowOpen', { id, action })
})

/**
 * Vista del contenido web de una ventana (estilo Electron).
 * Eventos: `did-finish-load`, `did-fail-load`, `did-start-navigation`,
 * `did-navigate`, `page-title-updated`, `before-input-event`.
 * `setWindowOpenHandler` controla `window.open`/`target=_blank`.
 */
export class WebContents extends EventEmitter {
  private _windowId: number | null

  constructor(windowId: number | null = null) {
    super()
    this._windowId = windowId
  }

  _setWindowId(id: number): void {
    this._windowId = id
  }

  get id(): number {
    return this._windowId ?? -1
  }

  /** Envía un evento SOLO a esta ventana (renderer: `ow.on(name, cb)`). */
  send(name: string, payload?: unknown): Promise<void> {
    if (this._windowId == null)
      return Promise.reject(new Error('ventana aún no creada'))
    return channel.call('node.emit', { name, payload, windowId: this._windowId })
  }

  loadURL(url: string): Promise<void> {
    return channel.call('window.loadURL', { windowId: this.id, url })
  }
  reload(): Promise<void> {
    return invokeNative('window', 'reload', this.id)
  }
  openDevTools(): Promise<void> {
    return invokeNative('window', 'openDevTools', this.id)
  }
  getURL(): Promise<string> {
    return invokeNative<string>('window', 'getURL', this.id)
  }
  getTitle(): Promise<string> {
    return invokeNative<string>('window', 'getTitle', this.id)
  }
  executeJavaScript<T = unknown>(js: string): Promise<T> {
    return channel.call<T>('window.eval', { windowId: this.id, js })
  }

  /** Captura la página (PNG) como `NativeImage` (resizable, toPNG/toDataURL). */
  async capturePage(): Promise<NativeImage> {
    const r = await channel.call<{ data: string }>('window.capturePage', {
      windowId: this.id,
      base64: true,
    })
    return nativeImage.createFromBuffer(Buffer.from(r.data, 'base64'))
  }

  /** Exporta la página a PDF (Buffer). */
  async printToPDF(_options: Record<string, unknown> = {}): Promise<Buffer> {
    const r = await channel.call<{ data: string }>('window.printToPDF', {
      windowId: this.id,
    })
    return Buffer.from(r.data, 'base64')
  }

  /** Abre el diálogo de impresión del sistema. */
  print(_options: Record<string, unknown> = {}): void {
    void invokeNative('window', 'print', this.id).catch(() => undefined)
  }

  /**
   * Controla `window.open`/`target=_blank`. Devuelve `{action:'deny'}` para
   * bloquear o `{action:'allow'}` para permitir (por defecto).
   */
  setWindowOpenHandler(handler: WindowOpenHandler | null): void {
    windowOpenHandler = handler ?? null
    void channel
      .call('webContents.setWindowOpenHandler', { windowId: this.id, enabled: !!handler })
      .catch(() => undefined)
  }
}

// ── BrowserWindow ───────────────────────────────────────────────────────────

/** Registro de ventanas por id (para resolver clicks de menú). */
const windowsById = new Map<number, BrowserWindow>()
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
    const params: Record<string, unknown> = { ...this._options }
    const res = await channel.call<{ windowId: number }>('window.create', params)
    this._id = res.windowId
    this._wc._setWindowId(res.windowId)
    windowsById.set(res.windowId, this)
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

// ── Menu / MenuItem (C5) ─────────────────────────────────────────────────────

export type MenuItemRole =
  | 'undo' | 'redo' | 'cut' | 'copy' | 'paste' | 'pasteAndMatchStyle' | 'selectAll'
  | 'delete' | 'reload' | 'forceReload' | 'toggleDevTools'
  | 'resetZoom' | 'zoomIn' | 'zoomOut' | 'togglefullscreen'
  | 'minimize' | 'close' | 'quit' | 'about' | 'hide' | 'hideOthers' | 'unhide'

export interface MenuItemConstructorOptions {
  id?: string
  label?: string
  role?: MenuItemRole
  type?: 'normal' | 'separator' | 'submenu' | 'checkbox' | 'radio'
  checked?: boolean
  enabled?: boolean
  visible?: boolean
  accelerator?: string
  sublabel?: string
  toolTip?: string
  submenu?: Array<MenuItemConstructorOptions | MenuItem> | Menu
  click?: (menuItem: MenuItem, window: BrowserWindow | undefined) => void
}

const menuItemsById = new Map<string, MenuItem>()

function formatAccelerator(acc: string): string {
  if (process.platform === 'darwin') return acc.replace(/CmdOrCtrl|CommandOrControl|Command|Cmd/g, 'Cmd')
  return acc.replace(/CmdOrCtrl|CommandOrControl|Command|Cmd/g, 'Ctrl')
}

export class MenuItem {
  id: string
  label: string
  role?: MenuItemRole
  type: 'normal' | 'separator' | 'submenu' | 'checkbox' | 'radio'
  checked: boolean
  enabled: boolean
  visible: boolean
  accelerator?: string
  sublabel?: string
  toolTip?: string
  submenu?: Menu
  click?: (menuItem: MenuItem, window: BrowserWindow | undefined) => void

  constructor(opts: MenuItemConstructorOptions = {}, idHint = 'm') {
    this.type = opts.type ?? (opts.submenu ? 'submenu' : 'normal')
    this.label = opts.label ?? opts.role ?? ''
    this.role = opts.role
    this.checked = !!opts.checked
    this.enabled = opts.enabled !== false
    this.visible = opts.visible !== false
    this.accelerator = opts.accelerator
    this.sublabel = opts.sublabel
    this.toolTip = opts.toolTip
    this.click = opts.click
    this.id = opts.id ?? idHint
    if (opts.submenu instanceof Menu) this.submenu = opts.submenu
    else if (Array.isArray(opts.submenu)) this.submenu = Menu.buildFromTemplate(opts.submenu, this.id)
  }

  _toJSON(): Record<string, unknown> {
    menuItemsById.set(this.id, this)
    const o: Record<string, unknown> = {
      id: this.id,
      label: this.label,
      type: this.type,
      enabled: this.enabled,
      visible: this.visible,
    }
    if (this.role) o.role = this.role
    if (this.checked) o.checked = true
    if (this.accelerator) o.accelerator = formatAccelerator(this.accelerator)
    if (this.submenu) o.submenu = this.submenu._toJSON()
    return o
  }
}

/**
 * Menú (estilo Electron, "Owear"): se construye en el main y los clicks llaman
 * handlers del main.
 *   Menu.setApplicationMenu(Menu.buildFromTemplate([...]))   // menubar (Windows)
 *   menu.popup({ window: win })                              // contextual
 */
export class Menu {
  items: MenuItem[] = []
  private static _appMenu: Menu | null = null

  constructor(items: MenuItem[] = []) {
    this.items = items
  }

  static buildFromTemplate(
    template: Array<MenuItemConstructorOptions | MenuItem>,
    idPrefix = 'm'
  ): Menu {
    const items = template.map((t, i) =>
      t instanceof MenuItem ? t : new MenuItem(t, `${idPrefix}.${i}`)
    )
    return new Menu(items)
  }

  static setApplicationMenu(menu: Menu | null): void {
    Menu._appMenu = menu
    const items = menu ? menu._toJSON() : []
    void channel.call('menu.setApplicationMenu', { items }).catch(() => undefined)
  }

  static getApplicationMenu(): Menu | null {
    return Menu._appMenu
  }

  append(item: MenuItem): void {
    this.items.push(item)
  }

  /** JSON del menú (registra los items para el dispatch de clicks). */
  _toJSON(): unknown[] {
    return this.items.map((it) => it._toJSON())
  }

  /** Menú contextual nativo. `window` resuelve el id del click. */
  popup(
    options: {
      window?: BrowserWindow
      x?: number
      y?: number
      items?: Array<MenuItemConstructorOptions | MenuItem>
    } = {}
  ): void {
    const items = options.items
      ? Menu.buildFromTemplate(options.items, 'popup')._toJSON()
      : this._toJSON()
    void channel
      .call('module.invoke', {
        module: 'menu',
        method: 'popup',
        args: [{ items, x: options.x, y: options.y }],
        windowId: options.window?.id ?? 0,
      })
      .catch(() => undefined)
  }
}

function runMenuItem(item: MenuItem, window: BrowserWindow | undefined): void {
  if (item.click) {
    item.click(item, window)
    return
  }
  if (!item.role) return
  switch (item.role) {
    case 'quit': void app.quit(); break
    case 'minimize': void window?.minimize(); break
    case 'close': void window?.close(); break
    case 'reload':
    case 'forceReload': void window?.webContents.reload(); break
    case 'toggleDevTools': void window?.webContents.openDevTools(); break
    case 'togglefullscreen': void window?.setFullScreen(true); break
    case 'undo': void window?.webContents.executeJavaScript("document.execCommand('undo')"); break
    case 'redo': void window?.webContents.executeJavaScript("document.execCommand('redo')"); break
    case 'cut': void window?.webContents.executeJavaScript("document.execCommand('cut')"); break
    case 'copy': void window?.webContents.executeJavaScript("document.execCommand('copy')"); break
    case 'paste': void window?.webContents.executeJavaScript("document.execCommand('paste')"); break
    case 'selectAll': void window?.webContents.executeJavaScript("document.execCommand('selectAll')"); break
    default: break
  }
}

function dispatchMenuClick(data: any): void {
  const id = data?.id
  if (!id) return
  const item = menuItemsById.get(id)
  if (!item) return
  const window = data.windowId ? windowsById.get(data.windowId) : undefined
  runMenuItem(item, window)
}

// Popup del módulo `menu` → module.event; menubar del kernel → menu.click.
channel.on('menu.click', (params: any) => dispatchMenuClick(params))

let currentTray: Tray | null = null

channel.on('module.event', (params: any) => {
  if (params?.name === 'menu.click') dispatchMenuClick(params.payload)
  else if (params?.name === 'tray.event' && currentTray)
    currentTray.emit(params.payload?.button ?? 'click', params.payload)
})

// ── Tray (C6) ───────────────────────────────────────────────────────────────

export interface TrayEvent {
  button: 'left' | 'right' | 'double'
  windowId: number
}

function toPngBase64(image: NativeImage | string): string {
  const img = typeof image === 'string' ? nativeImage.createFromPath(image) : image
  return img.toPNG().toString('base64')
}

/**
 * Icono de bandeja (intermedio Tauri/Electron). El menú contextual reutiliza
 * `Menu`. Eventos: `click`, `right-click`, `double-click`.
 *
 *   const tray = new Tray(nativeImage.createFromPath('icon.png'))
 *   tray.setContextMenu(Menu.buildFromTemplate([{ label: 'Mostrar', click: () => win.show() }]))
 *   tray.on('click', () => win.show())
 */
export class Tray extends EventEmitter {
  private _menu: Menu | null = null
  private _id: string

  constructor(image?: NativeImage | string) {
    super()
    this._id = `tray-${Math.random().toString(36).slice(2, 8)}`
    currentTray = this
    app
      .whenReady()
      .then(async () => {
        await invokeNative('tray', 'create', this._id).catch(() => undefined)
        if (image) this.setImage(image)
      })
      .catch(() => undefined)
  }

  get id(): string {
    return this._id
  }

  setImage(image: NativeImage | string): void {
    void invokeNative('tray', 'setImage', toPngBase64(image)).catch(() => undefined)
  }
  setPressedImage(image: NativeImage | string): void {
    void invokeNative('tray', 'setPressedImage', toPngBase64(image)).catch(() => undefined)
  }
  setToolTip(tip: string): void {
    void invokeNative('tray', 'setToolTip', tip).catch(() => undefined)
  }
  setTitle(title: string): void {
    void invokeNative('tray', 'setTitle', title).catch(() => undefined)
  }
  setContextMenu(menu: Menu | null): void {
    this._menu = menu
    void invokeNative('tray', 'setContextMenu', { items: menu ? menu._toJSON() : [] }).catch(
      () => undefined
    )
  }
  popupContextMenu(menu?: Menu): void {
    const m = menu ?? this._menu
    if (m) void invokeNative('tray', 'setContextMenu', { items: m._toJSON() }).catch(() => undefined)
    void invokeNative('tray', 'popupContextMenu').catch(() => undefined)
  }
  destroy(): void {
    if (currentTray === this) currentTray = null
    void invokeNative('tray', 'destroy').catch(() => undefined)
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
