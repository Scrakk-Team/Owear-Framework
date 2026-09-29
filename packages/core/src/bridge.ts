// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/bridge.ts — contrato (bridge) app ↔ installer ↔ uninstaller.
//
// El bridge es un fichero `owear.bridge.ts` (o `.js`) SIEMPRE presente en la
// app. `ow build installer` lo compila y lo embebe en el binario instalador
// como `bridge.json`, de modo que el instalador conoce la app sin adivinarla.
//
// El bridge es DATO puro (serializable a JSON): no metas funciones aquí. Los
// pasos que quieras ejecutar (hooks) se declaran por nombre y los implementa el
// propio instalador en su `app/main.ts`.

export type BridgePlatform = 'linux' | 'win' | 'mac' | (string & {})

/** Identidad de la app que se instala. */
export interface BridgeApp {
  /** Id estable/pkg-name (reverse-DNS recomendado): com.acme.miapp. */
  id: string
  name: string
  version: string
  publisher?: string
  /** Icono relativo al payload (png/svg/ico). */
  icon?: string
}

/** Formato de salida del payload que el instalador coloca. */
export type BridgeFormat = 'binary' | 'deb' | 'appimage' | 'exe' | 'msi'

/** Layout del payload: `minimal` = 1 binario; `layout` = árbol organizado. */
export type BridgeLayout = 'minimal' | 'layout'
export type BridgePreset = 'electron-like' | 'tauri-like' | 'flat' | 'custom'

/** Cómo resolver el runtime Node de la app instalada. */
export interface BridgeNode {
  mode?: 'system' | 'download' | 'embed'
}

/** Qué se compila/embebe y en qué orden (grupos lógicos). */
export type BridgeGroup =
  | 'kernel'
  | 'node'
  | 'modules:stock'
  | 'modules:app'
  | 'app:main'
  | 'app:workers'
  | 'app:assets'
  | 'resources'

/** Protección por grupo dentro del binario/payload. */
export interface BridgeProtect {
  integrity?: boolean
  readonly?: boolean
  hidden?: boolean
  signed?: boolean
}

/** Configuración por plataforma. */
export interface BridgeTarget {
  format?: BridgeFormat[]
  layout?: BridgeLayout
  /** Preset de estructura para `layout` (o 'custom'). */
  preset?: BridgePreset
  /** Directorio de instalación por defecto (per-user recomendado). */
  dir?: string
  shortcuts?: Array<'desktop' | 'menu' | 'startup' | 'startMenu'>
  /** Scope de instalación por defecto. */
  scope?: 'user' | 'system'
}

/**
 * Pasos que el instalador ejecuta. Son NOMBRES de hooks implementados en el
 * instalador (su `app/main.ts`), no funciones: el bridge debe ser serializable.
 */
export interface BridgeHooks {
  preInstall?: string
  postInstall?: string
  preUninstall?: string
  postUninstall?: string
}

/** Contrato completo app ↔ installer. */
export interface OwearBridge {
  /** Versión del formato del bridge (para compatibilidad). */
  bridgeVersion?: number
  app: BridgeApp
  targets: Partial<Record<BridgePlatform, BridgeTarget>>
  /** Orden de grupos al empaquetar/colocar (sobreescribe el preset). */
  order?: BridgeGroup[]
  /** Protección por grupo (integridad/flags). */
  protect?: Partial<Record<BridgeGroup, BridgeProtect>>
  node?: BridgeNode
  hooks?: BridgeHooks
}

/** Comprueba que el bridge es utilizable (falla pronto, en build). */
function validateBridge(b: OwearBridge): void {
  const err = (m: string): never => {
    throw new Error(`[owear.bridge] ${m}`)
  }
  if (!b || typeof b !== 'object') err('el bridge debe ser un objeto (export default)')
  if (!b.app?.id) err('app.id requerido')
  if (!b.app?.version) err('app.version requerido')
  if (!b.targets || typeof b.targets !== 'object') err('targets requerido')
  for (const [plat, t] of Object.entries(b.targets)) {
    if (t?.layout && t.layout !== 'minimal' && t.layout !== 'layout')
      err(`targets.${plat}.layout inválido: ${t.layout}`)
  }
  // Serializabilidad: el bridge se embebe como JSON (nada de funciones).
  try {
    JSON.stringify(b)
  } catch {
    err('el bridge no es serializable (¿metiste una función?)')
  }
}

/**
 * Define el bridge con tipado y validación.
 *
 *   // owear.bridge.ts
 *   import { defineBridge } from '@owear/core'
 *   export default defineBridge({
 *     app: { id: 'com.acme.miapp', name: 'MiApp', version: '1.0.0' },
 *     targets: {
 *       linux: { layout: 'minimal', format: ['binary'], shortcuts: ['desktop'] },
 *       win:   { layout: 'layout',  format: ['exe'],     shortcuts: ['startMenu'] },
 *     },
 *     order: ['kernel','modules:stock','app:main','app:assets'],
 *   })
 */
export function defineBridge(b: OwearBridge): OwearBridge {
  validateBridge(b)
  return { bridgeVersion: 1, ...b }
}
