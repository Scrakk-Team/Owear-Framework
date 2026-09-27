// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests de C4: nativeImage (códec PNG puro).

import { test } from 'node:test'
import assert from 'node:assert/strict'

import { nativeImage } from '../dist/index.js'

// PNG 1×1 RGBA rojo (base64 canónico).
const PNG_1x1 =
  'iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAEUlEQVR42mP4z/D/PwMYQxkAUMEI+IDuQRQAAAAASUVORK5CYII='

test('nativeImage: createFromBuffer/getSize/toPNG', () => {
  const img = nativeImage.createFromBuffer(Buffer.from(PNG_1x1, 'base64'))
  assert.deepEqual(img.getSize(), { width: 2, height: 2 })
  assert.equal(img.isEmpty(), false)
  assert.equal(img.toPNG()[0], 0x89)
})

test('nativeImage: resize y crop', () => {
  const img = nativeImage.createFromBuffer(Buffer.from(PNG_1x1, 'base64'))
  assert.deepEqual(img.resize({ width: 4, height: 4 }).getSize(), { width: 4, height: 4 })
  assert.deepEqual(img.resize({ width: 8 }).getSize(), { width: 8, height: 8 })
  assert.deepEqual(img.crop({ x: 0, y: 0, width: 1, height: 1 }).getSize(), {
    width: 1,
    height: 1,
  })
})

test('nativeImage: dataURL round-trip', () => {
  const img = nativeImage.createFromBuffer(Buffer.from(PNG_1x1, 'base64'))
  const back = nativeImage.createFromDataURL(img.toDataURL())
  assert.deepEqual(back.getSize(), { width: 2, height: 2 })
})

test('nativeImage: vacío si no existe', () => {
  assert.equal(nativeImage.createFromPath('/no/existe-owear.png').isEmpty(), true)
})
