// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// run.mjs — compila el main de Owear y arranca draw.io (port).
//
//   node run.mjs            → setup (si falta webapp) + build del main + kernel
//
// Env: OW_KERNEL_BIN (ruta al binario owear) si no está en build/<preset>.

import { spawn, spawnSync } from 'node:child_process'
import * as fs from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.dirname(fileURLToPath(import.meta.url))
const REPO = path.resolve(ROOT, '..', '..')
const WEBAPP = path.join(ROOT, 'webapp')
const OW = path.join(ROOT, '.owear')
const MAIN = path.join(OW, 'main.js')
const isWin = process.platform === 'win32'
const preset = isWin ? 'windows-release' : process.platform === 'darwin' ? 'macos-release' : 'linux-release'

function die(m) {
  console.error(`[drawio/run] ✗ ${m}`)
  process.exit(1)
}

// 1) webapp (setup si falta)
if (!fs.existsSync(path.join(WEBAPP, 'index.html'))) {
  const s = spawnSync(process.execPath, [path.join(ROOT, 'setup.mjs')], { stdio: 'inherit' })
  if (s.status !== 0) die('setup.mjs falló')
}

// 2) main.js (esbuild)
fs.mkdirSync(OW, { recursive: true })
const eb = spawnSync(
  'npx',
  ['esbuild', path.join(ROOT, 'app', 'main.ts'), '--bundle', '--platform=node', '--format=esm',
    `--outfile=${MAIN}`, '--log-level=warning'],
  { cwd: ROOT, stdio: 'inherit', shell: isWin },
)
if (eb.status !== 0) die('esbuild falló al compilar app/main.ts')

// 3) kernel
function findKernel() {
  if (process.env.OW_KERNEL_BIN && fs.existsSync(process.env.OW_KERNEL_BIN)) return process.env.OW_KERNEL_BIN
  const exe = isWin ? 'owear.exe' : 'owear'
  const c = path.join(REPO, 'build', preset, 'src', exe)
  return fs.existsSync(c) ? c : null
}
const kernel = findKernel()
if (!kernel) die('kernel no encontrado. Compílalo (cmake --preset … && cmake --build …) o define OW_KERNEL_BIN')

// 4) módulos stock
function stockModules() {
  const api = path.join(REPO, 'build', preset, 'src', 'api')
  if (!fs.existsSync(api)) return ''
  return fs
    .readdirSync(api, { withFileTypes: true })
    .filter((d) => d.isDirectory() && d.name !== 'CMakeFiles')
    .map((d) => path.join(api, d.name))
    .join(path.delimiter)
}

console.log(`[drawio/run] kernel: ${kernel}`)
const child = spawn(kernel, [], {
  stdio: 'inherit',
  env: {
    ...process.env,
    OW_APP_NAME: 'draw.io',
    OW_APP_ID: 'drawio',
    OW_ASSETS_DIR: WEBAPP,
    OW_APP_MAIN: MAIN,
    OW_MODULES_DIR: stockModules(),
  },
})
child.on('exit', (code) => process.exit(code ?? 0))
