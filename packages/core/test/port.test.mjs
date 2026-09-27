// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests de MessagePort / createChannel (A5): enrutado main ↔ main.

import { test } from 'node:test'
import assert from 'node:assert/strict'

import { app } from '../dist/index.js'

test('createChannel: los dos extremos se comunican', async () => {
  const { port1, port2 } = app.createChannel()
  assert.notEqual(port1.portId, port2.portId)

  const roundTrip = new Promise((resolve) => {
    port2.on('message', (m) => port2.postMessage({ reply: m }))
    port1.on('message', resolve)
  })

  port1.postMessage({ ping: 'hola' })
  assert.deepEqual(await roundTrip, { reply: { ping: 'hola' } })
})

test('createChannel: close deja de entregar', () => {
  const { port1, port2 } = app.createChannel()
  let got = 0
  port2.on('message', () => {
    got++
  })
  port1.postMessage(1)
  port1.close()
  port1.postMessage(2)
  assert.equal(got, 1)
})
