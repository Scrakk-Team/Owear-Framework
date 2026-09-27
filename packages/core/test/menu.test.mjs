// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests de C5: Menu/MenuItem (template, roles, aceleradores, ids).

import { test } from 'node:test'
import assert from 'node:assert/strict'

import { Menu, MenuItem } from '../dist/index.js'

test('Menu.buildFromTemplate: items, submenu y roles', () => {
  const menu = Menu.buildFromTemplate([
    { label: 'Archivo', submenu: [{ label: 'Nuevo' }, { role: 'quit' }] },
    { role: 'copy', accelerator: 'CmdOrCtrl+C' },
    { type: 'separator' },
    { label: 'Dev', type: 'checkbox', checked: true },
  ])
  const j = menu._toJSON()
  assert.equal(j.length, 4)
  assert.equal(j[0].label, 'Archivo')
  assert.equal(j[0].submenu[1].role, 'quit')
  assert.match(j[1].accelerator, /^C(trl|md)\+C$/)
  assert.equal(j[2].type, 'separator')
  assert.equal(j[3].checked, true)
})

test('Menu: ids estables (jerárquicos)', () => {
  const menu = Menu.buildFromTemplate([{ label: 'A', submenu: [{ label: 'B' }] }])
  const j = menu._toJSON()
  assert.equal(j[0].id, 'm.0')
  assert.equal(j[0].submenu[0].id, 'm.0.0')
})

test('Menu: acepta MenuItem ya construido y append', () => {
  const item = new MenuItem({ label: 'X' }, 'custom')
  const menu = Menu.buildFromTemplate([item])
  menu.append(new MenuItem({ role: 'quit' }, 'q'))
  const j = menu._toJSON()
  assert.equal(j[0].id, 'custom')
  assert.equal(j[1].role, 'quit')
})
