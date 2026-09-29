// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/updater/delta.ts — delta por bloques (estilo blockmap).
//
// El artefacto (p. ej. el binario único) se parte en bloques de tamaño fijo y
// se publica un blockmap con el sha256 de cada bloque. En el cliente se
// comparan los bloques locales con los remotos y **solo se descargan los que
// cambian** (HTTP Range), reutilizando el resto del fichero actual.

import { createHash } from 'node:crypto'

/** sha256 (hex) de cada bloque de `data` (tamaño `blockSize`). */
export function blockHashes(data: Buffer, blockSize: number): string[] {
  const out: string[] = []
  if (data.length === 0) {
    out.push(createHash('sha256').update(Buffer.alloc(0)).digest('hex'))
    return out
  }
  for (let off = 0; off < data.length; off += blockSize) {
    out.push(
      createHash('sha256')
        .update(data.subarray(off, Math.min(off + blockSize, data.length)))
        .digest('hex'),
    )
  }
  return out
}

/** Bloque de bytes [start, end] (end INCLUSIVE), como un HTTP Range. */
export interface ByteRange {
  start: number
  end: number
}

export interface DeltaPlan {
  blockSize: number
  total: number
  reused: number
  changed: number
  ranges: ByteRange[]
  /** Bytes a descargar si se aplica el delta. */
  downloadBytes: number
  /** Bytes del artefacto completo. */
  totalBytes: number
  /** Ratio de cambio (0..1). */
  ratio: number
}

/**
 * Plan de delta: compara los hashes locales con los remotos y agrupa los
 * bloques cambiados en rangos contiguos. `remoteSize` acota el último bloque.
 */
export function planDelta(
  localHashes: string[],
  remoteHashes: string[],
  blockSize: number,
  remoteSize: number,
): DeltaPlan {
  const total = remoteHashes.length
  const ranges: ByteRange[] = []
  let cur: ByteRange | null = null
  let changed = 0

  const blockEnd = (i: number): number => Math.min((i + 1) * blockSize, remoteSize) - 1

  for (let i = 0; i < total; i++) {
    const same = i < localHashes.length && localHashes[i] === remoteHashes[i]
    if (same) {
      if (cur) {
        ranges.push(cur)
        cur = null
      }
      continue
    }
    changed++
    const start = i * blockSize
    if (cur && start === cur.end + 1) cur.end = blockEnd(i)
    else {
      if (cur) ranges.push(cur)
      cur = { start, end: blockEnd(i) }
    }
  }
  if (cur) ranges.push(cur)

  const downloadBytes = ranges.reduce((n, r) => n + (r.end - r.start + 1), 0)
  return {
    blockSize,
    total,
    reused: total - changed,
    changed,
    ranges,
    downloadBytes,
    totalBytes: remoteSize,
    ratio: total ? changed / total : 1,
  }
}

/**
 * Ensambla el artefacto nuevo reutilizando los bloques locales y pidiendo los
 * cambiados con `fetchRange` (que debe devolver exactamente ese rango).
 */
export async function assemble(
  local: Buffer,
  remoteHashes: string[],
  plan: DeltaPlan,
  remoteSize: number,
  fetchRange: (r: ByteRange) => Promise<Buffer>,
): Promise<Buffer> {
  const out = Buffer.alloc(remoteSize)
  // bloque = reutilizado salvo que caiga dentro de un rango de descarga
  let ri = 0
  for (let i = 0; i < remoteHashes.length; i++) {
    const start = i * plan.blockSize
    const end = Math.min(start + plan.blockSize, remoteSize)
    while (ri < plan.ranges.length && plan.ranges[ri].end < start) ri++
    const inRange = ri < plan.ranges.length && plan.ranges[ri].start <= start
    if (!inRange) {
      // reutiliza del local (si existe y coincide); si no, se pedirá abajo
      if (start < local.length) local.copy(out, start, start, Math.min(end, local.length))
    }
  }
  // descarga de los rangos cambiados
  for (const r of plan.ranges) {
    const buf = await fetchRange(r)
    if (buf.length !== r.end - r.start + 1)
      throw new Error(`rango ${r.start}-${r.end}: recibidos ${buf.length} bytes`)
    buf.copy(out, r.start)
  }
  return out
}
