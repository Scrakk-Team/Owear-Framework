// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/updater.ts — autoUpdater (proceso principal).
//
// Estilo electron-updater pero para Owear:
//   autoUpdater.checkForUpdates() · downloadUpdate() · quitAndInstall()
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
//   size: 123456
//   blockSize: 262144        # delta por bloques
//   blockmap: https://…/MiApp-1.4.0.bin.blockmap
//   signature: <base64>      # Ed25519 sobre "<version>:<sha256>"
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
  signature?: string
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

interface UpdaterState {
  version?: string
  exe?: string
  dir?: string
  mode?: string
  platform?: string
  arch?: string
}

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

function canonical(version: string, sha256: string): Buffer {
  return Buffer.from(`${version}:${sha256}`)
}

class AutoUpdater extends EventEmitter {
  currentVersion = app.getVersion()
  channel = 'latest'
  allowPrerelease = false
  autoDownload = true
  autoInstallOnAppQuit = true
  forceDevUpdateConfig = false

  private feed: FeedOptions | null = null
  private info: UpdateInfo | null = null
  private newFile = ''

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

  /** Verifica la firma Ed25519 del manifiesto (sobre "<version>:<sha256>"). */
  private verifyManifest(info: UpdateInfo): boolean {
    const pub = this.feed?.publicKey
    if (!pub) return true // sin clave pública: no se exige (pero sha256 sí)
    if (!info.signature || !info.sha256) return false
    try {
      const key = pub.includes('BEGIN')
        ? crypto.createPublicKey(pub)
        : crypto.createPublicKey({ key: Buffer.from(pub, 'base64'), format: 'der', type: 'spki' })
      return crypto.verify(
        null,
        canonical(info.version, info.sha256),
        key,
        Buffer.from(info.signature, 'base64'),
      )
    } catch {
      return false
    }
  }

  async checkForUpdates(): Promise<UpdateCheckResult | null> {
    this.emit('checking-for-update')
    try {
      const res = await fetch(this.manifestUrl())
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
        file: { url: `${base}/${filePath}`, sha256: doc.sha256 != null ? String(doc.sha256) : undefined },
      }

      if (!version) throw new Error('manifiesto sin version')
      if (!this.verifyManifest(info)) throw new Error('firma del manifiesto inválida')

      this.info = info
      if (semverGt(version, this.currentVersion)) {
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
    const res = await fetch(url, { headers: { Range: `bytes=${r.start}-${r.end}` } })
    if (res.status !== 206 && !res.ok) throw new Error(`range ${r.start}-${r.end}: ${res.status}`)
    return Buffer.from(await res.arrayBuffer())
  }

  /** Descarga el update (delta por bloques si hay blockmap y binario local). */
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
      const bmRes = await fetch(info.blockmap)
      if (!bmRes.ok) throw new Error(`blockmap ${bmRes.status}`)
      const bm = JSON.parse(await bmRes.text()) as { blockSize: number; blocks: string[]; size: number }
      const localHashes = blockHashes(local, bm.blockSize)
      const plan = planDelta(localHashes, bm.blocks, bm.blockSize, bm.size)
      this.emit('download-progress', { total: plan.totalBytes, transferred: 0, percent: 0, bytesPerSecond: 0 })
      const started = Date.now()
      let done = 0
      data = await assemble(local, bm.blocks, plan, bm.size, async (r) => {
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
    } else {
      const res = await fetch(info.file.url)
      if (!res.ok) throw new Error(`descarga ${res.status}`)
      const total = Number(res.headers.get('content-length') ?? info.size ?? 0)
      const reader = res.body?.getReader()
      const chunks: Buffer[] = []
      let done = 0
      const started = Date.now()
      if (reader) {
        for (;;) {
          const { done: end, value } = await reader.read()
          if (end) break
          const b = Buffer.from(value)
          chunks.push(b)
          done += b.length
          const elapsed = (Date.now() - started) / 1000 || 0.001
          this.emit('download-progress', {
            total,
            transferred: done,
            percent: total ? Math.round((done / total) * 100) : 0,
            bytesPerSecond: Math.round(done / elapsed),
          })
        }
        data = Buffer.concat(chunks)
      } else {
        data = Buffer.from(await res.arrayBuffer())
      }
    }

    // Integridad (sha256 del manifiesto; sha512 opcional).
    if (info.sha256) {
      const got = crypto.createHash('sha256').update(data).digest('hex')
      if (got !== info.sha256) throw new Error('sha256 del update no coincide')
    }
    if (info.sha512) {
      const got = crypto.createHash('sha512').update(data).digest('hex')
      if (got !== info.sha512) throw new Error('sha512 del update no coincide')
    }

    fs.writeFileSync(dest, data)
    this.newFile = dest
    this.emit('update-downloaded', info)
    return [dest]
  }

  /**
   * Aplica el update (reemplazo atómico + relaunch) vía el módulo nativo.
   *
   * El kernel se reemplaza a sí mismo y se re-ejecuta (`execv`) dentro de esta
   * llamada, así que **no habrá respuesta**: la conexión cae. Lo tratamos como
   * éxito (el proceso nuevo arranca con el binario actualizado).
   */
  async quitAndInstall(): Promise<void> {
    if (!this.newFile) throw new Error('downloadUpdate() primero')
    try {
      await invokeNative('updater', 'apply', { path: this.newFile })
    } catch {
      // esperado: el proceso fue reemplazado y relanzado
    }
  }
}

export const autoUpdater = new AutoUpdater()
export { semverGt }
