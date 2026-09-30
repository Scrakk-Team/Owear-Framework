// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tipos de la API que el kernel inyecta en cada documento (`window.ow`).
// Types maintained by hand against docs/api/renderer.md.

export {}

declare global {
  interface Window {
    __owWindowId: number
    /** Presente cuando la ventana usa `titleBarOverlay` (botones nativos). */
    __owTitlebarOverlay?: {
      enabled: boolean
      height: number
      width: number
      top?: number
      right?: number
    }
    ow: {
      invoke(module: string, fn: string, ...args: unknown[]): Promise<unknown>
      invokeSync(module: string, fn: string, ...args: unknown[]): unknown
      readShared(handle: { id: string; size: number }): Promise<ArrayBuffer>
      on(name: string, cb: (payload: unknown) => void): () => void
      emit(name: string, payload?: unknown): void
      emitTo(targetWindowId: number, name: string, payload?: unknown): void
      findInPage(
        text: string,
        opts?: { matchCase?: boolean; backwards?: boolean }
      ): { matches: number; active: number }
    }
  }
}
