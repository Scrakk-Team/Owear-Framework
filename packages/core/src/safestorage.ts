// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/safestorage.ts — almacenamiento cifrado por el SO.
import { invokeNative } from './channel.js'

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


