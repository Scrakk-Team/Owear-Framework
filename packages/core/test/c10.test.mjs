// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests de C10: screen / powerMonitor / powerSaveBlocker (SDK, sin kernel).

import { test } from 'node:test'
import assert from 'node:assert/strict'

import { screen, powerMonitor, powerSaveBlocker } from '../dist/index.js'

test('powerSaveBlocker: start/stop/isStarted', () => {
  const id = powerSaveBlocker.start('prevent-display-sleep')
  assert.equal(typeof id, 'number')
  assert.equal(powerSaveBlocker.isStarted(id), true)
  assert.equal(powerSaveBlocker.stop(id), true)
  assert.equal(powerSaveBlocker.isStarted(id), false)
  // parar dos veces: la segunda no estaba activa
  assert.equal(powerSaveBlocker.stop(id), false)
})

test('powerSaveBlocker: ids distintos por bloqueador', () => {
  const a = powerSaveBlocker.start('prevent-app-suspension')
  const b = powerSaveBlocker.start('prevent-display-sleep')
  assert.notEqual(a, b)
  assert.equal(powerSaveBlocker.isStarted(a), true)
  assert.equal(powerSaveBlocker.isStarted(b), true)
  powerSaveBlocker.stop(a)
  powerSaveBlocker.stop(b)
})

test('screen: superficie de la API', () => {
  assert.equal(typeof screen.getAllDisplays, 'function')
  assert.equal(typeof screen.getPrimaryDisplay, 'function')
  assert.equal(typeof screen.getCursorScreenPoint, 'function')
  assert.equal(typeof screen.getDisplayNearestPoint, 'function')
  assert.equal(typeof screen.getDisplayMatching, 'function')
  assert.equal(typeof screen.screenToDipPoint, 'function')
  assert.equal(typeof screen.dipToScreenPoint, 'function')
  assert.equal(typeof screen.on, 'function')
  // alias
  assert.equal(typeof screen.getDisplays, 'function')
  assert.equal(typeof screen.nearestDisplay, 'function')
})

test('powerMonitor: superficie de la API', () => {
  assert.equal(typeof powerMonitor.getIdleTime, 'function')
  assert.equal(typeof powerMonitor.getIdleState, 'function')
  assert.equal(typeof powerMonitor.isOnBatteryPower, 'function')
  assert.equal(typeof powerMonitor.getSystemIdleTime, 'function')
  assert.equal(typeof powerMonitor.getSystemIdleState, 'function')
  assert.equal(typeof powerMonitor.on, 'function')
  assert.equal(powerMonitor.onBatteryPower, false)
})
