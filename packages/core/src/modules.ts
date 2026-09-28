// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/modules.ts — introspección de los módulos nativos cargados.
import { channel } from './channel.js'

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
