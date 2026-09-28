// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/tray.ts — Tray (C6).
import { EventEmitter } from 'node:events'
import { invokeNative } from './channel.js'
import { app } from './app.js'
import { Menu } from './menu.js'
import { NativeImage, nativeImage } from './nativeimage.js'

/** Tray actual (solo uno). Lo lee el ruteo de eventos del SDK. */
export let currentTray: Tray | null = null

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


