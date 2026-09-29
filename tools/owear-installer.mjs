#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// tools/owear-installer.mjs — ensambla el payload del INSTALADOR y lo empaqueta
// dentro del binario instalador (kernel + footer OWPK1).
//
// El instalador es un binario Owear aparte: al arrancar detecta `installer.json`
// (o `uninstaller.json`) y entra en modo installer/uninstaller (ver
// src/Core/App/Internal.cpp), sirviendo `ui/` como assets y exponiendo la API
// `installer` (src/api/installer).
//
// Layout del payload del instalador:
//   installer.json   metadatos de la app + modo/orden (del bridge)
//   bridge.json      contrato embebido (owear.bridge.ts compilado)
//   ui/              renderer + sidecar del instalador
//   payload/         lo que se instala (binario único [minimal] o árbol [layout])
//   modules/         módulos .owm extra (opcional)
//
// Uso:
//   node tools/owear-installer.mjs --kernel <owear> --out <MiAppInstaller> \
//     --ui <distUI> --payload <dir> [--bridge <bridge.json>] [--meta <installer.json>] \
//     [--modules <dir>] [--name <slug>]
//
import * as fs from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

function die(m) {
  console.error(`[owear-installer] ✗ ${m}`)
  process.exit(1)
}
const argv = process.argv.slice(2)
const arg = (n, def = null) => (argv.includes(n) ? argv[argv.indexOf(n) + 1] : def)

const kernel = arg('--kernel')
const out = arg('--out')
const ui = arg('--ui')
const payload = arg('--payload')
const bridge = arg('--bridge')
const meta = arg('--meta')
const modules = arg('--modules')
const name = arg('--name', 'app')

if (!kernel || !out || !ui) die('uso: --kernel <owear> --out <bin> --ui <distUI> [--payload <dir>]')
if (!fs.existsSync(kernel)) die(`no existe el kernel: ${kernel}`)
if (!fs.existsSync(ui)) die(`no existe el ui: ${ui}`)

const stage = fs.mkdtempSync(path.join(os.tmpdir(), 'owear-installer-'))
const copyInto = (src, dstRel) => {
  const dst = path.join(stage, dstRel)
  fs.mkdirSync(path.dirname(dst), { recursive: true })
  fs.cpSync(src, dst, { recursive: true })
}

copyInto(ui, 'ui')
if (payload && fs.existsSync(payload)) copyInto(payload, 'payload')
if (bridge && fs.existsSync(bridge)) fs.copyFileSync(bridge, path.join(stage, 'bridge.json'))
if (meta && fs.existsSync(meta)) fs.copyFileSync(meta, path.join(stage, 'installer.json'))
if (modules && fs.existsSync(modules)) copyInto(modules, 'modules')

// manifiesto mínimo del payload del instalador (informativo)
fs.writeFileSync(
  path.join(stage, 'manifest.json'),
  JSON.stringify({ kind: 'installer', name, createdAt: Date.now() }, null, 2) + '\n',
)

// Empaqueta con el empaquetador existente (mismo formato OWPK1).
const packer = path.join(__dirname, 'owear-pack.mjs')
const r = spawnSync(
  process.execPath,
  [packer, '--kernel', kernel, '--in', stage, '--out', out],
  { stdio: 'inherit' },
)
fs.rmSync(stage, { recursive: true, force: true })
if (r.status !== 0) die('falló owear-pack')
console.log(`[owear-installer] instalador: ${out}`)
