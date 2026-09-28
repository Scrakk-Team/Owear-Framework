// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/theme.ts — nativeTheme (módulo `theme`).
import { channel, invokeNative } from './channel.js'

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
  /** Fuerza la preferencia: 'system' | 'light' | 'dark'. También aplica el
   *  esquema al contenido (Windows: WebView2 `PreferredColorScheme`). */
  async setSource(source: 'system' | 'light' | 'dark'): Promise<ThemeInfo> {
    const info = await invokeNative<ThemeInfo>('theme', 'setSource', source)
    const scheme = source === 'dark' ? 2 : source === 'light' ? 1 : 0
    void channel.call('window.setColorScheme', { scheme }).catch(() => undefined)
    return info
  },
  watch(): Promise<void> {
    return invokeNative<void>('theme', 'watch')
  },
  unwatch(): Promise<void> {
    return invokeNative<void>('theme', 'unwatch')
  },
}

/** Alias estilo Electron de `theme`. */
export const nativeTheme = theme


