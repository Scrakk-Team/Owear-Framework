// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/charter.ts — capability policy for the renderer surface.
//
// A charter declares what a window's own document may reach in the kernel.
// It plays the role Electron gives to a hand-written preload (but enforced by
// the kernel, not by convention) and the role Tauri gives to a static
// capabilities file (but declared in TypeScript and applied at runtime, per
// window, and revocable).
//
//   const win = new BrowserWindow({
//     charter: { allow: [charterPresets.shell, 'fs:readText'] },
//   })
//
//   await win.setCharter({ allow: ['ow-window', 'theme', 'net'], deny: ['net:download'] })
//   await win.clearCharter()   // back to the default (no filtering)
//
import { channel } from './channel.js'

/** A grant: `module`, `module:fn`, `module:*` or `*`. */
export type CharterEntry = string

export interface Charter {
  /** Capabilities granted to the window's own document. */
  allow?: CharterEntry | CharterEntry[]
  /** Explicit refusals. They always win over `allow`. */
  deny?: CharterEntry | CharterEntry[]
  /**
   * Keeps the rules stored but paused. Defaults to `true` whenever the charter
   * declares rules, so a declared policy is a real policy.
   */
  enforce?: boolean
}

/** Normalized charter (what the kernel stores and returns). */
export interface CharterState {
  enforce: boolean
  allow: CharterEntry[]
  deny: CharterEntry[]
}

/**
 * Capabilities a window needs to keep its own chrome alive. Sprinkle them into
 * `allow` instead of remembering each module by heart.
 */
export const charterPresets = {
  /** Custom title bar buttons, move/resize drags and native theme queries. */
  shell: ['ow-window', 'theme'],
  /** Reading files, never writing them. */
  readOnlyFs: ['fs:readText', 'fs:readFile', 'fs:readDir', 'fs:stat', 'fs:exists'],
  /** Reach the main process through `app.handle` (the Node bridge). */
  node: ['node:call'],
} as const satisfies Record<string, readonly CharterEntry[]>

/** Accepted shape of every grant list: one entry or many. */
export type CharterEntries = CharterEntry | readonly CharterEntry[]

export function defineCharter(charter: Charter): CharterState {
  const allow = toArray(charter.allow)
  const deny = toArray(charter.deny)
  return {
    enforce: charter.enforce ?? allow.length + deny.length > 0,
    allow,
    deny,
  }
}

/** `'fs'` → `['fs']`; `['fs', 'theme']` → a copy. */
function toArray(entries: CharterEntries | undefined): CharterEntry[] {
  if (entries == null) return []
  if (typeof entries === 'string') return [entries]
  return [...entries]
}

// ── wire (used by BrowserWindow; exported so plain apps can script it) ──────

export interface CharterTarget {
  id: number | null
}

function requireId(target: CharterTarget): number {
  if (target.id == null) throw new Error('window is not created yet')
  return target.id
}

/** Installs (or replaces) a window charter. */
export async function setCharter(target: CharterTarget, charter: Charter): Promise<CharterState> {
  const spec = defineCharter(charter)
  return channel.call<CharterState>('window.setCharter', { windowId: requireId(target), ...spec })
}

/** Current charter of a window (`enforce:false` when it has none). */
export function getCharter(target: CharterTarget): Promise<CharterState> {
  return channel.call<CharterState>('window.getCharter', { windowId: requireId(target) })
}

/** Removes the charter: the window goes back to the default (no filtering). */
export function clearCharter(target: CharterTarget): Promise<CharterState> {
  return channel.call<CharterState>('window.clearCharter', { windowId: requireId(target) })
}

// ── audit ───────────────────────────────────────────────────────────────────

export interface CharterDenied {
  windowId: number
  /** `module:fn`, e.g. `fs:writeText`. */
  capability: string
  module: string
  fn: string
}

/**
 * Fires whenever the kernel refuses a call coming from a renderer. It is the
 * audit trail of the charter: log it, surface it in the UI, or fail the build
 * in CI when a call shows up in production.
 */
export function onCharterDenied(handler: (event: CharterDenied) => void): () => void {
  const listener = (params: unknown) => handler(params as CharterDenied)
  channel.on('charter.denied', listener)
  return () => channel.off('charter.denied', listener)
}
