// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// setup.mjs — prepara la webapp de draw.io para el port a Owear.
//
//   1. Si falta `webapp/`, la trae del repo `jgraph/drawio` (sparse: solo
//      `src/main/webapp`). 157 MB, por eso NO se commitea (ver .gitignore).
//   2. Copia el shim `owear-preload.js` dentro de la webapp.
//   3. Inyecta <script src="owear-preload.js"> antes de js/main.js (idempotente).
//
// Uso: node setup.mjs

import { spawnSync } from 'node:child_process'
import * as fs from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.dirname(fileURLToPath(import.meta.url))
const WEBAPP = path.join(ROOT, 'webapp')
const INDEX = path.join(WEBAPP, 'index.html')

function log(m) {
  console.log(`[drawio/setup] ${m}`)
}
function die(m) {
  console.error(`[drawio/setup] ✗ ${m}`)
  process.exit(1)
}

if (!fs.existsSync(INDEX)) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'drawio-web-'))
  log('clonando jgraph/drawio (sparse: src/main/webapp)…')
  const run = (args) => {
    const r = spawnSync('git', args, { cwd: tmp, stdio: 'inherit' })
    if (r.status !== 0) die(`git ${args.join(' ')} falló`)
  }
  run(['init', '-q'])
  run(['remote', 'add', 'origin', 'https://github.com/jgraph/drawio.git'])
  run(['config', 'core.sparseCheckout', 'true'])
  fs.writeFileSync(path.join(tmp, '.git', 'info', 'sparse-checkout'), 'src/main/webapp\n')
  run(['fetch', '--depth', '1', 'origin', 'dev'])
  run(['checkout', '-q', 'FETCH_HEAD'])
  const src = path.join(tmp, 'src', 'main', 'webapp')
  if (!fs.existsSync(src)) die('no se encontró src/main/webapp')
  fs.cpSync(src, WEBAPP, { recursive: true })
  fs.rmSync(tmp, { recursive: true, force: true })
  log('webapp lista en webapp/')
}

// 2) shim del preload
fs.copyFileSync(path.join(ROOT, 'owear-preload.js'), path.join(WEBAPP, 'owear-preload.js'))

// 3) parchea index.html (idempotente)
let html = fs.readFileSync(INDEX, 'utf8')
if (!html.includes('owear-preload.js')) {
  const anchor = '<script src="js/main.js"></script>'
  if (!html.includes(anchor)) die('no encuentro el anchor <script src="js/main.js"> en index.html')
  html = html.replace(anchor, '<script src="owear-preload.js"></script>\n' + anchor)
  fs.writeFileSync(INDEX, html)
  log('owear-preload.js inyectado en webapp/index.html')
} else {
  log('webapp/index.html ya tenía el shim')
}

log('listo. Arranca con: node run.mjs')
