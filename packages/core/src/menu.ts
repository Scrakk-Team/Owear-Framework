// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/menu.ts — Menu / MenuItem (C5).
import { channel } from './channel.js'
import { app } from './app.js'
import { BrowserWindow, windowsById } from './browserwindow.js'
import { screen } from './screen.js'
import { powerMonitor } from './power.js'
import { Tray, currentTray } from './tray.js'

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


channel.on('module.event', (params: any) => {
  const name = params?.name
  // Re-emisión genérica: `app.__channel.on('screen.added', …)`.
  if (typeof name === 'string') channel.emit(name, params?.payload)
  if (name === 'menu.click') dispatchMenuClick(params.payload)
  else if (name === 'tray.event' && currentTray)
    currentTray.emit(params.payload?.button ?? 'click', params.payload)
  // C10 — pantallas
  else if (name === 'screen.added') screen.emit('added', params.payload?.display)
  else if (name === 'screen.removed') screen.emit('removed', params.payload?.display)
  else if (name === 'screen.changed')
    screen.emit('changed', params.payload?.display, params.payload?.metrics ?? [])
  // C10 — energía
  else if (name === 'power.suspend') powerMonitor.emit('suspend')
  else if (name === 'power.resume') powerMonitor.emit('resume')
  else if (name === 'power.shutdown') powerMonitor.emit('shutdown')
  else if (name === 'power.lock') powerMonitor.emit('lock')
  else if (name === 'power.unlock') powerMonitor.emit('unlock')
  else if (name === 'power.ac') {
    powerMonitor._setOnBattery(false)
    powerMonitor.emit('ac')
  } else if (name === 'power.battery') {
    powerMonitor._setOnBattery(true)
    powerMonitor.emit('battery')
  }
})


