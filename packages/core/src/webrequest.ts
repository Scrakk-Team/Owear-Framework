// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/webrequest.ts — intercepción de requests (webRequest).
import { channel } from './channel.js'
import { app } from './index.js'

// ── webRequest (intercepción de requests) ───────────────────────────────────

export interface WebRequestDetails {
  id: number
  url: string
  method: string
  headers: Record<string, string>
}

export type WebRequestResult = { cancel?: boolean; redirectURL?: string } | void
export type WebRequestHandler = (
  details: WebRequestDetails
) => WebRequestResult | Promise<WebRequestResult>

interface WebRequestRegistration {
  patterns: string[]
  handler: WebRequestHandler
}

const webRequestRegistrations: WebRequestRegistration[] = []

function globMatch(pattern: string, url: string): boolean {
  if (pattern === '<all_urls>' || pattern === '*') return true
  const re =
    '^' +
    pattern
      .replace(/[.+^${}()|[\]\\]/g, '\\$&')
      .replace(/\*/g, '.*')
      .replace(/\?/g, '.') +
    '$'
  try {
    return new RegExp(re).test(url)
  } catch {
    return false
  }
}

function webRequestPatterns(): string[] {
  const set = new Set<string>()
  for (const r of webRequestRegistrations) for (const p of r.patterns) set.add(p)
  return [...set]
}

function syncWebRequest(): void {
  app
    .whenReady()
    .then(() => channel.call('webRequest.register', { urls: webRequestPatterns() }))
    .catch(() => undefined)
}

channel.on('webRequest.request', (params: any) => {
  const { id, url, method, headers } = params ?? {}
  const matched = webRequestRegistrations.filter((r) =>
    r.patterns.some((p) => globMatch(p, url))
  )
  Promise.resolve()
    .then(async () => {
      for (const r of matched) {
        const res = await r.handler({ id, url, method, headers: headers ?? {} })
        if (res && (res.cancel || res.redirectURL)) return res
      }
      return null
    })
    .then((res) =>
      channel.call('webRequest.respond', {
        id,
        cancel: !!(res && res.cancel),
        redirectURL: res && res.redirectURL,
      })
    )
    .catch(() => channel.call('webRequest.respond', { id, cancel: false }))
})

/**
 * Intercepción de requests (webRequest). `onBeforeRequest` puede cancelar o
 * redirigir. En **Windows** intercepta todos los requests; en **Linux** solo
 * navegaciones (WebKitGTK 2.52 ya no expone `send-request` para subrecursos).
 *
 *   webRequest.onBeforeRequest({ urls: ['https://ads.example.com/w'] }, () => ({ cancel: true }))
 */
export const webRequest = {
  onBeforeRequest(
    filter: { urls?: string[] } | string[],
    handler: WebRequestHandler
  ): () => void {
    const patterns = Array.isArray(filter) ? filter : filter?.urls ?? ['<all_urls>']
    const reg: WebRequestRegistration = { patterns, handler }
    webRequestRegistrations.push(reg)
    syncWebRequest()
    return () => {
      const i = webRequestRegistrations.indexOf(reg)
      if (i >= 0) webRequestRegistrations.splice(i, 1)
      syncWebRequest()
    }
  },
}


