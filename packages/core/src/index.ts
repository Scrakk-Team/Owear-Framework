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
  /** Fuerza la preferencia: 'system' | 'light' | 'dark'. */
  setSource(source: 'system' | 'light' | 'dark'): Promise<ThemeInfo> {
    return invokeNative<ThemeInfo>('theme', 'setSource', source)
  },
  watch(): Promise<void> {
    return invokeNative<void>('theme', 'watch')
  },
  unwatch(): Promise<void> {
    return invokeNative<void>('theme', 'unwatch')
  },
}

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
