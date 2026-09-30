// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests del guard de arranque (rollback): contador de intentos, detección de
// crash loop y confirmación de salud.

import { test } from 'node:test'
import assert from 'node:assert/strict'
import * as fs from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'

import { bootRecordPath, nextBoot, readBoot, writeBoot } from '../dist/updater/boot.js'

const now = () => new Date('2026-09-29T10:00:00Z')

test('boot: primer arranque de una versión → attempts=1 sin rollback', () => {
  const { record, shouldRollback } = nextBoot(null, '1.4.0', 3, now)
  assert.equal(record.version, '1.4.0')
  assert.equal(record.attempts, 1)
  assert.equal(shouldRollback, false)
})

test('boot: arranques repetidos de la misma versión acumulan y disparan rollback', () => {
  let rec = null
  const versions = []
  for (let i = 0; i < 4; i++) {
    const r = nextBoot(rec, '1.4.0', 3, now)
    versions.push({ attempts: r.record.attempts, rollback: r.shouldRollback })
    rec = r.record
  }
  assert.deepEqual(versions, [
    { attempts: 1, rollback: false },
    { attempts: 2, rollback: false },
    { attempts: 3, rollback: true },
    { attempts: 4, rollback: true },
  ])
})

test('boot: cambiar de versión reinicia el contador (update nuevo)', () => {
  const prev = { version: '1.3.0', attempts: 5, updatedAt: '' }
  const { record, shouldRollback } = nextBoot(prev, '1.4.0', 3, now)
  assert.equal(record.attempts, 1)
  assert.equal(shouldRollback, false)
})

test('boot: persistencia (read/write) e ida y vuelta', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'owear-boot-'))
  const file = bootRecordPath(dir)
  assert.equal(file, path.join(dir, 'update-boot.json'))
  assert.equal(readBoot(file), null)

  const rec = { version: '1.4.0', attempts: 2, updatedAt: now().toISOString() }
  writeBoot(file, rec)
  assert.deepEqual(readBoot(file), rec)

  // corrupto → null (no revienta)
  fs.writeFileSync(file, '{no-json')
  assert.equal(readBoot(file), null)

  fs.rmSync(dir, { recursive: true, force: true })
})

test('boot: registro corrupto no dispara rollback', () => {
  const { shouldRollback } = nextBoot(null, '1.4.0', 3, now)
  assert.equal(shouldRollback, false)
})
