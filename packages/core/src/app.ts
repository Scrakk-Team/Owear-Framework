// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/app.ts — ciclo de vida, rutas, identidad y commandLine.
import { EventEmitter } from 'node:events'
import * as os from 'node:os'
import * as path from 'node:path'
import * as fs from 'node:fs'
import { channel, socketPathFromEnv, invokeNative } from './channel.js'
import { forkWorker, type ForkWorkerOptions, type WorkerHandle } from './node/worker.js'
import { nodeHandlers, nodeContextHandlers } from './ipc/node.js'
import { setWindowIcon } from './appicon.js'
import { portRecords, makeMainPort, allocPortId } from './port.js'
import type { MessagePortMain } from './port.js'
import { protocolHandlers } from './protocol.js'
import type { ProtocolHandler } from './protocol.js'
import type { NodeHandler, ContextNodeHandler } from './ipc/node.js'

/** Resuelto por app.whenReady(). Compartido con BrowserWindow. */
export let readyPromise: Promise<void> | null = null

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

  /**
   * Define el icono por defecto de la app (ruta a PNG/JPEG). Se aplica a las
   * ventanas creadas a partir de entonces y a `new BrowserWindow()` sin `icon`.
   * (Equivale a `BrowserWindow({ icon })` global.)
   */
  setIcon(path: string): void {
    setWindowIcon(path)
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
    const a = allocPortId()
    const b = allocPortId()
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
export { invokeNative } from './channel.js'


