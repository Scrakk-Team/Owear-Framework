// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/installer.ts — API `installer` (modo instalador/desinstalador).
//
// Disponible en binarios compilados en modo installer/uninstaller (`OW_MODE`).
// El instalador es una app Owear aparte que lleva embebido el payload de la app
// y el bridge (contrato app ↔ installer).
//
//   import { installer } from '@owear/core'
//   const mode = await installer.mode()            // 'installer'
//   const plan = await installer.plan({ mode: 'minimal' })
//   await installer.install({ dir: '~/.local/opt/miapp', mode: 'minimal' })

import { invokeNative } from './channel.js'

export type InstallerMode = 'app' | 'installer' | 'uninstaller'

/** Entrada del plan de instalación (ruta en payload → destino). */
export interface PayloadEntry {
  /** Ruta relativa dentro del payload embebido. */
  rel: string
  /** Ruta destino (relativa al directorio de instalación). */
  dst: string
  size: number
  dir: boolean
}

/** Metadatos de la app que se instala (de `installer.json`). */
export interface InstallerInfo {
  appId?: string
  appName?: string
  version?: string
  mode?: InstallerMode
  publisher?: string
  icon?: string
  [k: string]: unknown
}

export interface PlanOptions {
  /** `minimal` = binario único; `layout` = árbol organizado. */
  mode?: 'minimal' | 'layout'
  /** `flat` = basename; `tree` = ruta relativa (default según mode). */
  layout?: 'flat' | 'tree'
  /** Orden de colocación (prefijos de ruta o grupos). */
  order?: string[]
}

export interface InstallOptions extends PlanOptions {
  /** Directorio de instalación. */
  dir: string
  /** Ruta del desinstalador (si se indica, se registra en el S.O.). */
  uninstaller?: string
  /** Publisher para el registro de desinstalación. */
  publisher?: string
}

export interface InstallResult {
  installed: boolean
  dir: string
  mode: string
  files: number
}

export interface UninstallResult {
  removed: number
  dir: string
}

export interface VerifyResult {
  ok: boolean
  mismatches: string[]
}

export interface StateResult {
  installed: boolean
  version?: string
  mode?: string
  app?: string
}

export interface ShortcutOptions {
  execPath: string
  iconPath?: string
  /** Anulan los del manifiesto (por defecto: appId/appName del installer.json). */
  appId?: string
  appName?: string
  desktop?: boolean
  menu?: boolean
  startup?: boolean
}

export interface LaunchOptions {
  path: string
  args?: string[]
}

/**
 * API del modo instalador. Requiere un binario compilado como instalador
 * (payload embebido + `OW_MODE=installer`).
 */
export const installer = {
  /** Modo del binario actual: 'app' | 'installer' | 'uninstaller'. */
  mode(): Promise<InstallerMode> {
    return invokeNative<InstallerMode>('installer', 'mode')
  },

  /** Metadatos de la app a instalar (config empaquetada por `ow build`). */
  info(): Promise<InstallerInfo> {
    return invokeNative<InstallerInfo>('installer', 'info')
  },

  /** Bridge embebido (contrato app ↔ installer). */
  bridge<T = unknown>(): Promise<T | null> {
    return invokeNative<T | null>('installer', 'bridge')
  },

  /** Directorio de instalación por defecto (absoluto, por usuario). */
  defaultDir(): Promise<string> {
    return invokeNative<string>('installer', 'defaultDir')
  },

  /** Selector NATIVO de carpeta (no depende de módulos .owm). null si cancela. */
  chooseDir(opts: { title?: string; defaultPath?: string } = {}): Promise<string | null> {
    return invokeNative<string | null>('installer', 'chooseDir', opts)
  },

  /** Lista los ficheros del payload embebido. */
  payloadList(): Promise<PayloadEntry[]> {
    return invokeNative<PayloadEntry[]>('installer', 'payloadList')
  },

  /** Lee un fichero del payload (base64). */
  payloadRead(path: string): Promise<{ path: string; data: string; size: number }> {
    return invokeNative('installer', 'payloadRead', { path })
  },

  /** Plan de instalación (sin escribir nada). */
  plan(opts: PlanOptions = {}): Promise<PayloadEntry[]> {
    return invokeNative<PayloadEntry[]>('installer', 'plan', opts)
  },

  /** Instala el payload en `dir` (respeta `order`/`layout`). */
  install(opts: InstallOptions): Promise<InstallResult> {
    return invokeNative<InstallResult>('installer', 'install', opts)
  },

  /** Desinstala (usa el manifiesto guardado en `dir`). */
  uninstall(opts: { dir: string; keepData?: boolean }): Promise<UninstallResult> {
    return invokeNative<UninstallResult>('installer', 'uninstall', opts)
  },

  /** Verifica integridad de una instalación (hashes). */
  verify(opts: { dir: string }): Promise<VerifyResult> {
    return invokeNative<VerifyResult>('installer', 'verify', opts)
  },

  /** Estado de instalación en `dir`. */
  state(opts: { dir: string }): Promise<StateResult> {
    return invokeNative<StateResult>('installer', 'state', opts)
  },

  /** Crea accesos directos / integración de escritorio. */
  shortcuts(opts: ShortcutOptions): Promise<boolean> {
    return invokeNative<boolean>('installer', 'shortcuts', opts)
  },

  /** Lanza un ejecutable en segundo plano (p. ej. la app instalada). */
  launch(opts: LaunchOptions): Promise<boolean> {
    return invokeNative<boolean>('installer', 'launch', opts)
  },

  /** ¿El proceso corre elevado (root/admin)? */
  elevate(): Promise<{ elevated: boolean }> {
    return invokeNative<{ elevated: boolean }>('installer', 'elevate')
  },
}
