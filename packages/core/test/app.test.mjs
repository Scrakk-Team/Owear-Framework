// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests de C1: rutas de `app.getPath`, overrides y commandLine.

import { test } from 'node:test'
import assert from 'node:assert/strict'
import * as path from 'node:path'

import { app } from '../dist/index.js'

test('app.getPath devuelve rutas absolutas', () => {
  for (const name of [
    'home',
    'userData',
    'temp',
    'logs',
    'downloads',
    'documents',
    'cache',
    'exe',
    'appPath',
  ]) {
    const p = app.getPath(name)
    assert.ok(p && path.isAbsolute(p), `${name} -> ${p}`)
  }
})

test('app.setPath sobrescribe getPath', () => {
  app.setPath('userData', path.join('/tmp', 'ow-test-ud'))
  assert.equal(app.getPath('userData'), path.join('/tmp', 'ow-test-ud'))
})

test('app.commandLine get/has', () => {
  app.commandLine.appendSwitch('ow-test', 'v')
  assert.equal(app.commandLine.getSwitchValue('ow-test'), 'v')
  assert.ok(app.commandLine.hasSwitch('ow-test'))
})

test('app.getName/setName', () => {
  const before = app.getName()
  app.setName('ow-test-app')
  assert.equal(app.getName(), 'ow-test-app')
  app.setName(before)
})
