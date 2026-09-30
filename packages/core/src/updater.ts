// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/updater.ts — autoUpdater (proceso principal).
//
// Estilo electron-updater pero para Owear:
//   autoUpdater.checkForUpdates() · downloadUpdate() · quitAndInstall()
//   autoUpdater.rollback() · commitUpdate() · armBootGuard()
//   eventos: checking-for-update · update-available · update-not-available ·
//            error · download-progress · update-downloaded
//
// Manifiesto YAML por canal (`<channel>.yml`, p. ej. latest.yml):
//   version: 1.4.0
//   releaseDate: '2026-09-29T…'
//   notes: |
//     …
//   path: MiApp-1.4.0.bin
//   sha256: <hex>            # integridad del artefacto
//   sha512: <hex>
//   size: 123456
//   blockSize: 262144        # delta por bloques
//   blockmap: https://…/MiApp-1.4.0.bin.blockmap
//   signature: <base64>      # Ed25519 sobre "<version>:<sha256>" (metadata)
//   binarySig: <base64>      # Ed25519 sobre los bytes del artefacto (payload)
//
// La firma se verifica con la clave pública del bridge (`updater.publicKey`).

import { EventEmitter } from 'node:events'
import * as crypto from 'node:crypto'
import * as fs from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'
import { app } from './app.js'
import { invokeNative } from './channel.js'
import { parseYaml, type YamlMap } from './updater/yaml.js'
import { assemble, blockHashes, planDelta, type ByteRange } from './updater/delta.js'
import { bootRecordPath, nextBoot, readBoot, writeBoot, type BootRecord } from './updater/boot.js'

export interface FeedOptions {
  provider?: 'generic' | 'github'
  /** Base del feed (generic) o `owner/repo` (github). */
  url: string
  channel?: string
  /** Clave pública Ed25519 (PEM o base64 SPKI). */
  publicKey?: string
}

export interface UpdateFile {
  url: string
  sha256?: string
  sha512?: string
  size?: number
}

export interface UpdateInfo {
  version: string
  notes?: string
  releaseDate?: string
  mandatory?: boolean
  sha256?: string
  sha512?: string
  size?: number
  blockSize?: number
  blockmap?: string
  /** Firma Ed25519 de la metadata: `"<version>:<sha256>"`. */
  signature?: string
  /** Firma Ed25519 del artefacto completo (defensa en profundidad). */
  binarySig?: string
  file: UpdateFile
}

export interface ProgressInfo {
  total: number
  transferred: number
  percent: number
  bytesPerSecond: number
}

export interface UpdateCheckResult {
  updateInfo: UpdateInfo
}

export interface InstallOptions {
  /** Guarda una copia del binario actual para poder revertir (rollback). */
  backup?: boolean
}

export interface BootGuardOptions {
  /** Arranques sin confirmar antes de revertir. */
  threshold?: number
  /** ms de vida "sana" tras los que se confirma el update. */
  healthDelayMs?: number
}

interface UpdaterState {
  version?: string
  exe?: string
  dir?: string
  mode?: string
  platform?: string
  arch?: string
  hasRollback?: boolean
}

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms))

function semverGt(a: string, b: string): boolean {
  const p = (s: string): number[] => {
    const core = s.replace(/^v/, '').split('-')[0]
    const parts = core.split('.').map((n) => parseInt(n, 10) || 0)
    while (parts.length < 3) parts.push(0)
    return parts
  }
  const [x, y] = [p(a), p(b)]
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] > y[i]
  return false
}

function isPrerelease(v: string): boolean {
  return v.replace(/^v/, '').includes('-')
}

function canonical(version: string, sha256: string): Buffer {
  return Buffer.from(`${version}:${sha256}`)
}

const RETRYABLE = new Set([408, 425, 429, 500, 502, 503, 504])

class AutoUpdater extends EventEmitter {
  currentVersion = app.getVersion()
  channel = 'latest'
  allowPrerelease = false
  autoDownload = true
  autoInstallOnAppQuit = true
  /** Nº de reintentos por petición (con backoff exponencial). */
  maxRetries = 3
  /** Espera base entre reintentos (ms); se duplica cada intento. */
  retryDelay = 800
  /** Timeout por petición (ms). */
  requestTimeout = 60_000

  private feed: FeedOptions | null = null
  private info: UpdateInfo | null = null
  private newFile = ''
  private healthTimer: NodeJS.Timeout | null = null

  setFeedURL(opts: FeedOptions): void {
    this.feed = opts
    if (opts.channel) this.channel = opts.channel
  }

  private manifestUrl(): string {
    if (!this.feed) throw new Error('autoUpdater.setFeedURL() primero')
    const { provider = 'generic', url } = this.feed
    if (provider === 'github')
      return `https://github.com/${url}/releases/latest/download/${this.channel}.yml`
    return `${url.replace(/\/+$/, '')}/${this.channel}.yml`
  }

  private publicKey(): crypto.KeyObject | null {
    const pub = this.feed?.publicKey
    if (!pub) return null
    try {
      if (pub.includes('BEGIN')) return crypto.createPublicKey(pub)
      return crypto.createPublicKey({
        key: Buffer.from(pub, 'base64'),
        format: 'der',
        type: 'spki',
      })
    } catch {
      return null
    }
  }

  /** Verifica la firma Ed25519 del manifiesto (sobre `"<version>:<sha256>"`). */
  private verifyManifest(info: UpdateInfo): boolean {
    const key = this.publicKey()
    if (!key) return true // sin clave pública: no se exige (pero sha256 sí)
    if (!info.signature || !info.sha256) return false
    try {
      return crypto.verify(null, canonical(info.version, info.sha256), key, Buffer.from(info.signature, 'base64'))
    } catch {
      return false
    }
  }

  private backoff(attempt: number): Promise<void> {
    const ms = Math.min(this.retryDelay * 2 ** (attempt - 1), 15_000) + Math.floor(Math.random() * 250)
    return sleep(ms)
  }

  /**
   * `fetch` con timeout y reintentos (backoff exponencial) para errores de red
   * y estados transitorios. Devuelve la respuesta (incluidos 4xx no
   * reintentables, para que el llamante dé un error concreto).
   */
  private async request(url: string, init: RequestInit = {}, what = 'petición'): Promise<Response> {
    let lastErr: unknown
    for (let attempt = 0; ; attempt++) {
      const ctrl = new AbortController()
      const timer = setTimeout(() => ctrl.abort(), this.requestTimeout)
      try {
        const res = await fetch(url, { ...init, signal: ctrl.signal })
        if (RETRYABLE.has(res.status) && attempt < this.maxRetries) {
          await this.backoff(attempt + 1)
          continue
        }
        return res
      } catch (e) {
        lastErr = e
        if (attempt >= this.maxRetries) throw e
        await this.backoff(attempt + 1)
      } finally {
        clearTimeout(timer)
      }
    }
    throw lastErr instanceof Error ? lastErr : new Error(`${what} falló`)
  }

  async checkForUpdates(): Promise<UpdateCheckResult | null> {
    this.emit('checking-for-update')
    try {
      const res = await this.request(this.manifestUrl(), {}, 'manifiesto')
      if (!res.ok) throw new Error(`feed ${res.status} ${res.statusText}`)
      const text = await res.text()
      const doc = parseYaml(text) as YamlMap
      if (!doc || typeof doc !== 'object') throw new Error('manifiesto YAML inválido')

      const version = String(doc.version ?? '')
      const filePath = String(doc.path ?? '')
      const base = this.manifestUrl().replace(/\/[^/]+$/, '')
      const info: UpdateInfo = {
        version,
        notes: doc.notes != null ? String(doc.notes) : undefined,
        releaseDate: doc.releaseDate != null ? String(doc.releaseDate) : undefined,
        mandatory: doc.mandatory === true,
        sha256: doc.sha256 != null ? String(doc.sha256) : undefined,
        sha512: doc.sha512 != null ? String(doc.sha512) : undefined,
        size: typeof doc.size === 'number' ? doc.size : undefined,
        blockSize: typeof doc.blockSize === 'number' ? doc.blockSize : undefined,
        blockmap: doc.blockmap != null ? String(doc.blockmap) : undefined,
        signature: doc.signature != null ? String(doc.signature) : undefined,
        binarySig: doc.binarySig != null ? String(doc.binarySig) : undefined,
        file: {
          url: `${base}/${filePath}`,
          sha256: doc.sha256 != null ? String(doc.sha256) : undefined,
        },
      }

      if (!version) throw new Error('manifiesto sin version')
      if (!this.verifyManifest(info)) throw new Error('firma del manifiesto inválida')
      if (info.binarySig && !this.publicKey()) throw new Error('binarySig sin clave pública con la que verificar')

      this.info = info
      const isNew = semverGt(version, this.currentVersion)
      const preBlocked = isPrerelease(version) && !this.allowPrerelease && !isPrerelease(this.currentVersion)
      if (isNew && !preBlocked) {
        this.emit('update-available', info)
        if (this.autoDownload) void this.downloadUpdate().catch((e) => this.emit('error', e))
        return { updateInfo: info }
      }
      this.emit('update-not-available', info)
      return null
    } catch (e) {
      this.emit('error', e instanceof Error ? e : new Error(String(e)))
      return null
    }
  }

  private async fetchRange(url: string, r: ByteRange): Promise<Buffer> {
    const res = await this.request(url, { headers: { Range: `bytes=${r.start}-${r.end}` } }, 'rango')
    if (res.status !== 206)
      throw new Error(`el servidor no soporta Range (${res.status} al pedir ${r.start}-${r.end})`)
    const buf = Buffer.from(await res.arrayBuffer())
    if (buf.length !== r.end - r.start + 1)
      throw new Error(`rango ${r.start}-${r.end}: recibidos ${buf.length} bytes`)
    return buf
  }

  /** Delta por bloques: blockmap + reutilización local + rangos. */
  private async downloadDelta(info: UpdateInfo, local: Buffer): Promise<Buffer> {
    const bmRes = await this.request(info.blockmap!, {}, 'blockmap')
    if (!bmRes.ok) throw new Error(`blockmap ${bmRes.status}`)
    const bm = JSON.parse(await bmRes.text()) as { blockSize: number; blocks: string[]; size: number }
    const plan = planDelta(blockHashes(local, bm.blockSize), bm.blocks, bm.blockSize, bm.size)
    this.emit('download-progress', { total: plan.totalBytes, transferred: 0, percent: 0, bytesPerSecond: 0 })
    const started = Date.now()
    let done = 0
    return assemble(local, bm.blocks, plan, bm.size, async (r) => {
      const buf = await this.fetchRange(info.file.url, r)
      done += buf.length
      const elapsed = (Date.now() - started) / 1000 || 0.001
      this.emit('download-progress', {
        total: plan.totalBytes,
        transferred: done,
        percent: plan.totalBytes ? Math.round((done / plan.totalBytes) * 100) : 100,
        bytesPerSecond: Math.round(done / elapsed),
      })
      return buf
    })
  }

  /**
   * Descarga completa con **reanudación**: si la conexión cae a mitad, reintenta
   * con `Range: bytes=<recibido>-` y continúa en vez de empezar de cero.
   */
  private async downloadFull(info: UpdateInfo): Promise<Buffer> {
    const url = info.file.url
    const started = Date.now()
    let chunks: Buffer[] = []
    let got = 0
    let lastErr: unknown
    for (let attempt = 0; attempt <= this.maxRetries; attempt++) {
      try {
        const headers: Record<string, string> = {}
        if (got > 0) headers.Range = `bytes=${got}-`
        const res = await this.request(url, { headers }, 'descarga')
        if (!res.ok && res.status !== 206) throw new Error(`descarga ${res.status}`)
        if (got > 0 && res.status !== 206) {
          chunks = []
          got = 0 // el servidor ignoró el Range
        }
        const total = Number(res.headers.get('content-length') ?? 0) + got || info.size || 0
        const reader = res.body?.getReader()
        if (!reader) {
          const buf = Buffer.from(await res.arrayBuffer())
          chunks.push(buf)
          got += buf.length
          return Buffer.concat(chunks)
        }
        for (;;) {
          const { done, value } = await reader.read()
          if (done) break
          const b = Buffer.from(value)
          chunks.push(b)
          got += b.length
          const elapsed = (Date.now() - started) / 1000 || 0.001
          this.emit('download-progress', {
            total,
            transferred: got,
            percent: total ? Math.min(100, Math.round((got / total) * 100)) : 0,
            bytesPerSecond: Math.round(got / elapsed),
          })
        }
        return Buffer.concat(chunks)
      } catch (e) {
        lastErr = e
        if (attempt >= this.maxRetries) break
        await this.backoff(attempt + 1) // continúa desde `got` (resume)
      }
    }
    throw lastErr instanceof Error ? lastErr : new Error('descarga falló')
  }

  /** Descarga el update (delta si hay blockmap; si no, completa con resume). */
  async downloadUpdate(): Promise<string[]> {
    const info = this.info
    if (!info) throw new Error('checkForUpdates() primero')

    const state = await invokeNative<UpdaterState>('updater', 'state').catch(() => ({}) as UpdaterState)
    const dest = path.join(os.tmpdir(), `owear-update-${info.version}${path.extname(info.file.url) || '.bin'}`)

    let local = Buffer.alloc(0)
    if (info.blockmap && info.blockSize && state.exe) {
      try {
        local = fs.readFileSync(state.exe)
      } catch {
        local = Buffer.alloc(0)
      }
    }

    let data: Buffer
    if (info.blockmap && info.blockSize && local.length > 0) {
      try {
        data = await this.downloadDelta(info, local)
      } catch {
        // sin Range / blockmap inválido → descarga completa
        data = await this.downloadFull(info)
      }
    } else {
      data = await this.downloadFull(info)
    }

    // Integridad (sha256; sha512 opcional) y firma del payload (binarySig).
    if (info.sha256) {
      const got = crypto.createHash('sha256').update(data).digest('hex')
      if (got !== info.sha256) throw new Error('sha256 del update no coincide')
    }
    if (info.sha512) {
      const got = crypto.createHash('sha512').update(data).digest('hex')
      if (got !== info.sha512) throw new Error('sha512 del update no coincide')
    }
    if (info.binarySig) {
      const key = this.publicKey()
      const ok = key && crypto.verify(null, data, key, Buffer.from(info.binarySig, 'base64'))
      if (!ok) throw new Error('firma del binario (binarySig) inválida')
    }

    fs.writeFileSync(dest, data)
    this.newFile = dest
    this.emit('update-downloaded', info)
    if (this.autoInstallOnAppQuit) {
      app.once('before-quit', () => void this.quitAndInstall().catch(() => undefined))
    }
    return [dest]
  }

  /**
   * Aplica el update (reemplazo atómico + relaunch) vía el módulo nativo.
   *
   * El kernel se reemplaza a sí mismo y se re-ejecuta (`execv`) dentro de esta
   * llamada, así que **no habrá respuesta**: la conexión cae. Lo tratamos como
   * éxito (el proceso nuevo arranca con el binario actualizado).
   */
  async quitAndInstall(opts: InstallOptions = {}): Promise<void> {
    if (!this.newFile) throw new Error('downloadUpdate() primero')
    const backup = opts.backup !== false
    try {
      await invokeNative('updater', 'apply', { path: this.newFile, backup })
    } catch {
      // esperado: el proceso fue reemplazado y relanzado
    }
  }

  /**
   * Revierte al binario anterior (`<exe>.owprev`) y relanza. Solo tiene efecto
   * si hay una copia de seguridad pendiente (p. ej. tras un update con backup).
   */
  async rollback(): Promise<void> {
    try {
      await invokeNative('updater', 'rollback')
    } catch {
      // el proceso fue reemplazado y relanzado
    }
  }

  /** Confirma el update (borra la copia de seguridad) → desarma el rollback. */
  async commitUpdate(): Promise<void> {
    if (this.healthTimer) {
      clearTimeout(this.healthTimer)
      this.healthTimer = null
    }
    await invokeNative('updater', 'commit').catch(() => undefined)
  }

  /**
   * Arma el **guard de arranque** tras un update: si el binario nuevo se
   * reinicia sin llegar a "sano" (crash loop) más de `threshold` veces,
   * revierte. Si arranca bien `healthDelayMs`, confirma (borra el backup).
   *
   *   app.whenReady().then(() => autoUpdater.armBootGuard())
   */
  async armBootGuard(opts: BootGuardOptions = {}): Promise<'idle' | 'armed' | 'rolled-back'> {
    const threshold = opts.threshold ?? 3
    const healthDelayMs = opts.healthDelayMs ?? 10_000
    const st = await invokeNative<UpdaterState>('updater', 'state').catch(() => ({}) as UpdaterState)
    if (!st.hasRollback) return 'idle' // sin update pendiente

    const file = bootRecordPath(app.getPath('userData'))
    const next = nextBoot(readBoot(file), this.currentVersion, threshold)
    if (next.shouldRollback) {
      await this.rollback()
      return 'rolled-back'
    }
    writeBoot(file, next.record)
    this.healthTimer = setTimeout(() => void this.commitUpdate(), healthDelayMs)
    this.healthTimer.unref?.()
    return 'armed'
  }
}

export const autoUpdater = new AutoUpdater()
export { semverGt, isPrerelease }
export type { BootRecord }
