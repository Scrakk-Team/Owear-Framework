// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests de `app.forkWorker` (A1): worker Node con canal de mensajes.

import { test } from 'node:test'
import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'

import { forkWorker } from '../dist/index.js'

const fixture = (name) => join(dirname(fileURLToPath(import.meta.url)), 'fixtures', name)

test('forkWorker: round-trip vía process.parentPort', async () => {
  const w = forkWorker(fixture('echo-worker.mjs'))
  try {
    const got = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('timeout del worker')), 8000)
      w.on('message', (m) => {
        clearTimeout(timer)
        resolve(m)
      })
      w.postMessage({ hola: 1 })
    })
    assert.deepEqual(got, { echo: { hola: 1 } })
  } finally {
    w.kill()
  }
})

test('forkWorker: evento exit con código', async () => {
  const w = forkWorker(fixture('exit-worker.mjs'))
  const code = await new Promise((resolve) => w.on('exit', resolve))
  assert.equal(code, 3)
})
