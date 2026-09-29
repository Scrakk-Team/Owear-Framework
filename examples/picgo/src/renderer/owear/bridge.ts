// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/renderer/owear/bridge.ts — ADAPTADOR Electron → Owear.
//
// PicGo (Electron) hablaba con el main por `window.bridgeApi` (preload). En
// Owear no hay preload: el kernel inyecta `window.ow`. Este módulo reproduce el
// MISMO `bridgeApi` sobre `window.ow` para que el renderer real de PicGo no
// cambie: `ipc.invoke/send/on` se reenvían al sidecar Node (`ow.invoke('node',
// 'call', { fn, args })`) e i18n/webUtils/webFrame se emulan aquí.
//
// Importar SIEMPRE antes de usar `./utils/bridge` (va primero en main.tsx).

import { I18n } from '@picgo/i18n/dist/i18n'
import { ObjectAdapter } from '@picgo/i18n/dist/adapters/object'

type Listener = (...args: unknown[]) => void
type Cleanup = () => void

interface OwBridge {
  invoke<T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T>
  on(name: string, cb: (payload: unknown) => void): Cleanup
  off?(name: string, cb: (payload: unknown) => void): void
  emit?(name: string, payload?: unknown): void
}

const ow = (window as unknown as { ow: OwBridge }).ow

/** Llama al sidecar Node; si el canal no está implementado devuelve null. */
async function call<T>(channel: string, args: unknown[]): Promise<T> {
  try {
    return (await ow.invoke<T>('node', 'call', { fn: channel, args })) as T
  } catch (e) {
    // El renderer de PicGo espera poder arrancar aunque falte un handler.
    console.debug('[owear/bridge] canal sin handler:', channel, e)
    return null as unknown as T
  }
}

const ipc = {
  send: (channel: string, ...args: unknown[]): void => {
    void call(channel, args)
  },
  invoke: <T,>(channel: string, ...args: unknown[]): Promise<T> => call<T>(channel, args),
  on: (channel: string, listener: Listener): Cleanup => {
    const unsub = ow.on(channel, listener)
    return typeof unsub === 'function' ? unsub : () => {}
  },
  once: (channel: string, listener: Listener): Cleanup => {
    let off: Cleanup = () => {}
    off = ipc.on(channel, (...a: unknown[]) => {
      off()
      listener(...a)
    })
    return off
  },
  removeAllListeners: (_channel: string): void => {
    /* Owear no expone off global; se limpia al cerrar la ventana */
  },
}

const platform = ((): NodeJS.Platform => {
  const p = navigator.userAgent.toLowerCase()
  return p.includes('win') ? 'win32' : p.includes('mac') ? 'darwin' : 'linux'
})()

const bridgeApi = {
  ipc,
  webUtils: {
    // El navegador embebido puede exponer `path` en File; si no, cadena vacía.
    getPathForFile: (file: File): string =>
      (file as unknown as { path?: string }).path ?? '',
  },
  webFrame: {
    setVisualZoomLevelLimits: (_min: number, _max: number): void => {},
  },
  env: {
    platform,
    isDev:
      Boolean((window as unknown as { OW_DEV_SERVER_URL?: string }).OW_DEV_SERVER_URL) ||
      Boolean(import.meta.env?.DEV),
  },
  i18n: {
    ObjectAdapter: {
      create: (locales: Record<string, unknown>) => new ObjectAdapter(locales as never),
    },
    I18n: {
      createFromLocales: (locales: Record<string, unknown>, defaultLanguage: string) => {
        const i18n = new I18n({
          adapter: new ObjectAdapter(locales as never),
          defaultLanguage,
        })
        return {
          getLanguage: () => i18n.getLanguage(),
          setLanguage: (language: string) => i18n.setLanguage(language),
          setDefaultLanguage: (language: string) => i18n.setDefaultLanguage(language),
          translate: (key: never, args: Record<string, unknown> = {}) =>
            i18n.translate(key, args as never) || (key as unknown as string),
        }
      },
    },
  },
}

;(window as unknown as { bridgeApi: typeof bridgeApi }).bridgeApi = bridgeApi

export {}
