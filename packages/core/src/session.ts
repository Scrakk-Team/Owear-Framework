// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/session.ts — permisos del WebView y particiones.
import { channel } from './channel.js'
import { app } from './index.js'

// ── session (permisos del WebView) ──────────────────────────────────────────

export interface PermissionRequest {
  id: number
  /** geolocation | notifications | media | microphone | camera | clipboardRead | … */
  permission: string
  origin: string
}

export type PermissionHandler = (req: PermissionRequest) => boolean | Promise<boolean>

const permissionHandlers: PermissionHandler[] = []

channel.on('session.permissionRequest', (params: any) => {
  const { id, permission, origin } = params ?? {}
  Promise.all(
    permissionHandlers.map((h) => Promise.resolve(h({ id, permission, origin })))
  )
    .then((results) =>
      channel.call('session.respondPermission', { id, allow: results.some(Boolean) })
    )
    .catch(() =>
      channel.call('session.respondPermission', { id, allow: false })
    )
})

/**
 * Permisos del WebView (geolocalización, notificaciones, cámara, micrófono…).
 * El handler decide allow/deny; si no hay handler registrado, se deniega.
 *   session.onPermissionRequest(({ permission, origin }) => permission === 'notifications')
 */
export const session = {
  /**
   * Referencia a una partición de sesión. Pásala al crear la ventana:
   *   const s = session.fromPartition('persist:cuenta-2')
   *   new BrowserWindow({ session: s.partition, url })
   * Aísla cookies/storage/cache por perfil (WebKit: data dir; WebView2: perfil).
   */
  fromPartition(partition: string): { partition: string } {
    return { partition }
  },

  onPermissionRequest(handler: PermissionHandler): () => void {
    permissionHandlers.push(handler)
    // El handler se registra ya; avisar al kernel en cuanto haya canal.
    app
      .whenReady()
      .then(() => channel.call('session.setPermissionHandler', { enabled: true }))
      .catch(() => undefined)
    return () => {
      const i = permissionHandlers.indexOf(handler)
      if (i >= 0) permissionHandlers.splice(i, 1)
    }
  },
}


