// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests of the charter SDK surface: normalization, presets and audit wiring.

import { test } from 'node:test'
import assert from 'node:assert/strict'

import {
  defineCharter,
  charterPresets,
  onCharterDenied,
} from '../dist/charter.js'

test('defineCharter normalizes a single entry to a list', () => {
  const spec = defineCharter({ allow: 'fs:readText' })
  assert.deepEqual(spec.allow, ['fs:readText'])
  assert.deepEqual(spec.deny, [])
})

test('defineCharter keeps lists and copies them', () => {
  const allow = ['ow-window', 'theme']
  const spec = defineCharter({ allow, deny: ['theme:set'] })
  assert.deepEqual(spec.allow, allow)
  assert.deepEqual(spec.deny, ['theme:set'])
  assert.notEqual(spec.allow, allow, 'the input array must not be aliased')
})

test('a declared charter is enforced by default', () => {
  assert.equal(defineCharter({ allow: ['ow-window'] }).enforce, true)
  assert.equal(defineCharter({ deny: ['net'] }).enforce, true)
})

test('an empty charter is not enforced', () => {
  assert.equal(defineCharter({}).enforce, false)
})

test('enforce can be paused explicitly', () => {
  const spec = defineCharter({ allow: ['fs'], enforce: false })
  assert.equal(spec.enforce, false)
  assert.deepEqual(spec.allow, ['fs'], 'rules survive while paused')
})

test('presets expand to listable entries', () => {
  assert.ok(charterPresets.shell.includes('ow-window'))
  assert.ok(charterPresets.readOnlyFs.includes('fs:readText'))
  assert.deepEqual(charterPresets.node, ['node:call'])
  for (const entry of charterPresets.readOnlyFs)
    assert.match(entry, /^fs:/, 'fs presets are function-scoped, never the module')
})

test('onCharterDenied returns an unsubscribe function', () => {
  const off = onCharterDenied(() => undefined)
  assert.equal(typeof off, 'function')
  off() // must not throw
})
