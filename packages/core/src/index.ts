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
import { channel, socketPathFromEnv, invokeNative } from './channel.js'

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


export { invokeNative } from './channel.js'
export { listNativeModules, nativeModuleInfo } from './modules.js'
export type { NativeModuleInfo } from './modules.js'
export { theme, nativeTheme } from './theme.js'
export type { ThemeInfo } from './theme.js'

export { screen } from './screen.js'
export type { DisplayBounds, DisplaySize, Display, DisplayMetric } from './screen.js'

export { powerMonitor, powerSaveBlocker } from './power.js'
export type { IdleState, PowerSaveBlockerType } from './power.js'

export { safeStorage } from './safestorage.js'
export type { SafeStorageResult } from './safestorage.js'

export { session } from './session.js'
export type { PermissionRequest, PermissionHandler } from './session.js'

export { webRequest } from './webrequest.js'
export type { WebRequestDetails, WebRequestResult, WebRequestHandler } from './webrequest.js'

export { dialog } from './dialog.js'
export type { FileFilter, OpenDialogOptions, SaveDialogOptions, MessageBoxOptions } from './dialog.js'

export { WebContents } from './webcontents.js'

export { BrowserWindow } from './browserwindow.js'

export { MenuItem, Menu } from './menu.js'

export { Tray } from './tray.js'
export type { TrayEvent } from './tray.js'

export { nodeHandlers, nodeContextHandlers } from './ipc/node.js'
export type { NodeHandler, ContextNodeHandler } from './ipc/node.js'

export { protocolHandlers } from './protocol.js'
export type { ProtocolRequest, ProtocolHandlerResult, ProtocolHandler } from './protocol.js'

export { portRecords, makeMainPort, allocPortId } from './port.js'
export type { MessagePortMain } from './port.js'

export { app } from './app.js'
export { readyPromise } from './app.js'

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
