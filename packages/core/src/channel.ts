// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/channel.ts — canal de control (NDJSON) con el kernel.
//
// Piedra angular del SDK: sin dependencias de otras partes del paquete (evita
// ciclos). El resto de features importan `channel` / `invokeNative` de aquí.
import * as net from 'node:net'
import { EventEmitter } from 'node:events'
import * as fs from 'node:fs'
import * as path from 'node:path'

type PendingEntry = { resolve: (v: any) => void; reject: (e: Error) => void }

export class ControlChannel extends EventEmitter {
  private socket: net.Socket | null = null
  private buffer = ''
  private nextId = 1
  private pending = new Map<number, PendingEntry>()

  connect(socketPath: string): Promise<void> {
    return new Promise((resolve, reject) => {
      const sock = net.connect(socketPath)
      sock.once('connect', () => {
        this.socket = sock
        resolve()
      })
      sock.once('error', (err: Error) => {
        this.socket = null
        this.rejectAllPending(err)
        reject(err)
      })
      sock.on('data', (chunk: Buffer) => {
        this.buffer += chunk.toString('utf8')
        let idx: number
        while ((idx = this.buffer.indexOf('\n')) >= 0) {
          const line = this.buffer.slice(0, idx)
          this.buffer = this.buffer.slice(idx + 1)
          if (line.trim()) this.handleLine(line)
        }
      })
      sock.on('close', () => {
        this.socket = null
        this.rejectAllPending(new Error('conexión cerrada'))
        this.emit('disconnected')
      })
    })
  }

  private rejectAllPending(err: Error) {
    for (const { reject } of this.pending.values()) {
      reject(err)
    }
    this.pending.clear()
  }

  private handleLine(line: string) {
    let msg: any
    try {
      msg = JSON.parse(line)
    } catch {
      return
    }
    if (msg.event) {
      this.emit(msg.event, msg.params)
      return
    }
    const p = this.pending.get(msg.id)
    if (!p) return
    this.pending.delete(msg.id)
    if (msg.ok) p.resolve(msg.result)
    else p.reject(new Error(msg.error ?? 'error de control'))
  }

  call<T = any>(cmd: string, params: Record<string, unknown> = {}): Promise<T> {
    if (!this.socket) return Promise.reject(new Error('control socket desconectado'))
    const id = this.nextId++
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve, reject })
      this.socket!.write(JSON.stringify({ id, cmd, params }) + '\n')
    })
  }
}

/** Canal singleton con el kernel. */
export const channel = new ControlChannel()

/** Ruta del control socket (env o último de XDG_RUNTIME_DIR). */
export function socketPathFromEnv(): string {
  if (process.env.OW_CONTROL_SOCKET) return process.env.OW_CONTROL_SOCKET
  // fallback: último socket vivo en XDG_RUNTIME_DIR (útil para depurar)
  const dir = process.env.XDG_RUNTIME_DIR
  if (dir) {
    try {
      const socks = fs
        .readdirSync(dir)
        .filter((f) => f.startsWith('owear-') && f.endsWith('.sock'))
        .sort()
      if (socks.length) return path.join(dir, socks[socks.length - 1])
    } catch {
      /* noop */
    }
  }
  throw new Error(
    'Owear: OW_CONTROL_SOCKET no definida. Lanza la app con `ow dev` o `ow build && owear`.'
  )
}

/**
 * Invoca un módulo nativo (.owm) desde el proceso principal. Equivalente a
 * `ow.invoke()` del renderer, pero sin depender de una ventana.
 *
 *   const txt = await invokeNative<string>('fs', 'readText', '/etc/hostname')
 */
export function invokeNative<T = unknown>(
  module: string,
  method: string,
  ...args: unknown[]
): Promise<T> {
  return channel.call<T>('module.invoke', { module, method, args })
}
