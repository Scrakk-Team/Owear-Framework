// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/webcontents.ts — objeto webContents (C3).
import { EventEmitter } from 'node:events'
import { channel, invokeNative } from './channel.js'
import { NativeImage, nativeImage } from './nativeimage.js'

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
export const WC_EVENT_NAMES: Record<string, string> = {
  didFinishLoad: 'did-finish-load',
  didFailLoad: 'did-fail-load',
  navigationStarted: 'did-start-navigation',
  loadCommitted: 'did-navigate',
  pageTitleUpdated: 'page-title-updated',
  beforeInput: 'before-input-event',
}

/** Eventos de ventana → nombres estilo Electron (dash) que emite BrowserWindow. */
export const WIN_EVENT_NAMES: Record<string, string> = {
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


