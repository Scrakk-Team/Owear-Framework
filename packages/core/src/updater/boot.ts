// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/updater/boot.ts — registro de arranque para el **rollback**.
//
// Tras aplicar un update el binario nuevo arranca con una copia de seguridad
// (`<exe>.owprev`). Si el arranque se repite sin que la app marque "sano"
// (crash loop), el updater revierte al binario anterior.
//
// El contador vive en `<userData>/update-boot.json`:
//   { "version": "1.4.0", "attempts": 2, "updatedAt": "…" }
// Cada arranque incrementa `attempts`; cuando la app confirma salud
// (`commit`), se pone a 0. Si `attempts` alcanza el umbral → rollback.

import * as fs from 'node:fs'
import * as path from 'node:path'

export interface BootRecord {
  version: string
  attempts: number
  updatedAt: string
}

/** Ruta del registro de arranque dentro de `<userData>`. */
export function bootRecordPath(userDataDir: string): string {
  return path.join(userDataDir, 'update-boot.json')
}

/** Lee el registro; null si no existe o está corrupto. */
export function readBoot(file: string): BootRecord | null {
  try {
    const raw = JSON.parse(fs.readFileSync(file, 'utf8')) as Partial<BootRecord>
    if (typeof raw?.version !== 'string' || typeof raw?.attempts !== 'number') return null
    return { version: raw.version, attempts: raw.attempts, updatedAt: String(raw.updatedAt ?? '') }
  } catch {
    return null
  }
}

/** Escribe el registro (crea el directorio si hace falta). */
export function writeBoot(file: string, rec: BootRecord): void {
  fs.mkdirSync(path.dirname(file), { recursive: true })
  fs.writeFileSync(file, JSON.stringify(rec) + '\n')
}

/**
 * Calcula el estado del siguiente arranque:
 * - misma versión que la anterior → `attempts + 1`; si llega al umbral, rollback.
 * - versión distinta (o primer arranque) → `attempts = 1`, sin rollback.
 */
export function nextBoot(
  prev: BootRecord | null,
  version: string,
  threshold: number,
  now: () => Date = () => new Date(),
): { record: BootRecord; shouldRollback: boolean } {
  const sameVersion = !!prev && prev.version === version
  const attempts = sameVersion ? prev!.attempts + 1 : 1
  return {
    record: { version, attempts, updatedAt: now().toISOString() },
    shouldRollback: sameVersion && attempts >= threshold,
  }
}
