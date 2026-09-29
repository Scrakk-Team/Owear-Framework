// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests del auto-updater (SDK, sin kernel): YAML, semver, delta por bloques y
// firma Ed25519.

import { test } from 'node:test'
import assert from 'node:assert/strict'
import * as crypto from 'node:crypto'

import { semverGt } from '../dist/updater.js'
import { parseYaml } from '../dist/updater/yaml.js'
import { blockHashes, planDelta, assemble } from '../dist/updater/delta.js'

const MANIFEST = `version: 1.4.0
releaseDate: '2026-09-29T10:00:00Z'
notes: |
  Arreglos varios
  y mejoras
path: MiApp-1.4.0.bin
sha256: deadbeef
size: 1000000
blockSize: 262144
blockmap: https://up.test/MiApp-1.4.0.bin.blockmap
signature: c2ln
files:
  - url: https://up.test/a.bin
    size: 10
  - url: https://up.test/b.bin
`

test('yaml: parsea el manifiesto', () => {
  const m = parseYaml(MANIFEST)
  assert.equal(m.version, '1.4.0')
  assert.equal(m.path, 'MiApp-1.4.0.bin')
  assert.equal(m.size, 1000000)
  assert.equal(m.blockSize, 262144)
  assert.equal(m.releaseDate, '2026-09-29T10:00:00Z')
  assert.match(m.notes, /Arreglos varios\ny mejoras/)
  assert.equal(m.signature, 'c2ln')
  assert.equal(Array.isArray(m.files), true)
  assert.equal(m.files.length, 2)
  assert.equal(m.files[0].url, 'https://up.test/a.bin')
  assert.equal(m.files[0].size, 10)
  assert.equal(m.files[1].url, 'https://up.test/b.bin')
})

test('semver: comparación', () => {
  assert.equal(semverGt('1.4.0', '1.3.9'), true)
  assert.equal(semverGt('1.4.0', '1.4.0'), false)
  assert.equal(semverGt('2.0.0', '1.9.9'), true)
  assert.equal(semverGt('1.4.1', '1.4.0'), true)
  assert.equal(semverGt('v1.5.0', '1.4.0'), true)
})

test('delta: solo cambia el bloque modificado', () => {
  const bs = 1024
  const local = Buffer.alloc(bs * 4, 0x41)
  const remote = Buffer.from(local)
  remote.fill(0x42, bs, bs * 2) // cambia el bloque 1

  const remoteHashes = blockHashes(remote, bs)
  const plan = planDelta(blockHashes(local, bs), remoteHashes, bs, remote.length)

  assert.equal(plan.total, 4)
  assert.equal(plan.changed, 1)
  assert.equal(plan.reused, 3)
  assert.equal(plan.ranges.length, 1)
  assert.deepEqual(plan.ranges[0], { start: bs, end: bs * 2 - 1 })
  assert.equal(plan.downloadBytes, bs)
})

test('delta: assemble reconstruye el remoto (reusa + rangos)', async () => {
  const bs = 512
  const local = Buffer.alloc(bs * 3, 0x10)
  const remote = Buffer.from(local)
  remote.fill(0x20, 0, bs)          // bloque 0 cambia
  remote.fill(0x30, bs * 2, bs * 3) // bloque 2 cambia

  const remoteHashes = blockHashes(remote, bs)
  const plan = planDelta(blockHashes(local, bs), remoteHashes, bs, remote.length)
  assert.equal(plan.changed, 2)

  const out = await assemble(local, remoteHashes, plan, remote.length, async (r) =>
    Buffer.from(remote.subarray(r.start, r.end + 1)),
  )
  assert.equal(Buffer.compare(out, remote), 0)
})

test('delta: bloque reutilizado aunque la lista local sea corta', () => {
  const bs = 256
  const remote = Buffer.alloc(bs * 2, 0x55)
  const plan = planDelta([], blockHashes(remote, bs), bs, remote.length)
  assert.equal(plan.changed, 2)
  assert.equal(plan.ranges.length, 1)
  assert.equal(plan.downloadBytes, bs * 2)
})

test('firma: Ed25519 sobre "<version>:<sha256>"', () => {
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519')
  const canonical = Buffer.from('1.4.0:deadbeef')
  const sig = crypto.sign(null, canonical, privateKey)

  assert.equal(crypto.verify(null, canonical, publicKey, sig), true)
  assert.equal(crypto.verify(null, Buffer.from('1.4.0:otro'), publicKey, sig), false)

  // export/import SPKI base64 (lo que viaja en el bridge)
  const spki = publicKey.export({ format: 'der', type: 'spki' }).toString('base64')
  const pub2 = crypto.createPublicKey({
    key: Buffer.from(spki, 'base64'),
    format: 'der',
    type: 'spki',
  })
  assert.equal(crypto.verify(null, canonical, pub2, sig), true)
})
