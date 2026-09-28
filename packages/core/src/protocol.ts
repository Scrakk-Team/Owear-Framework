// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/protocol.ts — esquemas personalizados (protocol).
import { channel } from './channel.js'

// ── protocol (esquemas personalizados) ──────────────────────────────────────
export interface ProtocolRequest {
  url: string
  method: string
  headers: Record<string, string>
  /** Cuerpo de la request en base64 (formato del bridge). */
  body: string
}

export type ProtocolHandlerResult =
  | Response
  | { status?: number; headers?: Record<string, string>; body?: string | Uint8Array }
  | string
  | null
  | undefined

export type ProtocolHandler = (
  req: ProtocolRequest
) => ProtocolHandlerResult | Promise<ProtocolHandlerResult>

export const protocolHandlers = new Map<string, ProtocolHandler>()

async function normalizeProtocolResponse(
  res: ProtocolHandlerResult
): Promise<{ status: number; headers: Record<string, string>; body: string }> {
  if (res == null) return { status: 404, headers: {}, body: '' }
  if (typeof res === 'string')
    return { status: 200, headers: { 'content-type': 'text/html' }, body: toB64(res) }
  if (typeof Response !== 'undefined' && res instanceof Response) {
    const headers: Record<string, string> = {}
    res.headers.forEach((v, k) => {
      headers[k] = v
    })
    const body = Buffer.from(await res.arrayBuffer()).toString('base64')
    return { status: res.status, headers, body }
  }
  const r = res as { status?: number; headers?: Record<string, string>; body?: string | Uint8Array }
  let body = ''
  if (r.body instanceof Uint8Array) body = Buffer.from(r.body).toString('base64')
  else if (typeof r.body === 'string') body = toB64(r.body)
  return { status: r.status ?? 200, headers: r.headers ?? {}, body }
}

function toB64(text: string): string {
  return Buffer.from(text, 'utf-8').toString('base64')
}

channel.on('protocol.request', (params: any) => {
  const { reqId, scheme, url, method, headers, body } = params ?? {}
  const handler = protocolHandlers.get(scheme)
  Promise.resolve()
    .then(() =>
      handler
        ? handler({ url, method, headers: headers ?? {}, body: body ?? '' })
        : null
    )
    .then((res) => normalizeProtocolResponse(res ?? null))
    .then((out) =>
      channel.call('protocol.respond', {
        reqId,
        status: out.status,
        headers: out.headers,
        body: out.body,
      })
    )
    .catch(() =>
      channel.call('protocol.respond', { reqId, status: 500, headers: {}, body: '' })
    )
})


