// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/port.ts — MessagePort (main ↔ renderer).
import { EventEmitter } from 'node:events'
import { channel } from './channel.js'
import { nodeHandlers } from './ipc/node.js'

// ── MessagePort (canal bidireccional main ↔ renderer) ───────────────────────
// Cada endpoint vive o en el main o en un renderer. Los mensajes se enrutan por
// el bridge. El payload viaja como JSON (igual que el resto del bridge); el
// transporte binario sin copia kernel→renderer sigue siendo `ow-shm://`.
export interface MessagePortMain {
  /** Id global del endpoint (para transferirlo a un renderer). */
  readonly portId: number
  /** Envía un mensaje al otro extremo. */
  postMessage(message: unknown): void
  on(event: 'message', listener: (message: unknown) => void): this
  start(): void
  close(): void
}

type PortSide = { kind: 'main' } | { kind: 'renderer'; windowId: number }
interface PortRecord {
  id: number
  peer: number
  side: PortSide
  emitter: EventEmitter
}

export const portRecords = new Map<number, PortRecord>()
export let nextPortId = 1

/** Reserva un id de puerto nuevo (evita asignar el import fuera del módulo). */
export function allocPortId(): number {
  return nextPortId++
}

function deliverPort(fromId: number, data: unknown): void {
  const rec = portRecords.get(fromId)
  if (!rec) return
  const peer = portRecords.get(rec.peer)
  if (!peer) return
  if (peer.side.kind === 'main') {
    peer.emitter.emit('message', data)
  } else {
    void channel.call('node.emit', {
      name: '__ow_port:msg',
      payload: { id: peer.id, data },
      windowId: peer.side.windowId,
    })
  }
}

export function makeMainPort(id: number): MessagePortMain {
  const rec = portRecords.get(id)!
  return Object.assign(rec.emitter, {
    portId: id,
    postMessage: (data: unknown) => deliverPort(id, data),
    start: () => undefined,
    close: () => portRecords.delete(id),
  }) as unknown as MessagePortMain
}

// Handler reservado: el renderer publica en un puerto con
// `ow.invoke('node','call',{ fn:'__ow_port_post', args:[{ id, data }] })`.
nodeHandlers.set('__ow_port_post', (msg: { id: number; data: unknown }) => {
  if (msg && typeof msg.id === 'number') deliverPort(msg.id, msg.data)
  return null
})


