// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// dev-installer.mjs — itera el UI del instalador SIN re-empaquetar el binario.
//
// Arranca `vite` (dev server del installer/) y lanza el binario instalador ya
// construido apuntando a ese dev server (OW_DEV_SERVER_URL). Así tienes
// hot-reload del UI y el `installer` API real (payload del binario).
//
//   node dev-installer.mjs
//
// Requiere que exista el binario instalador; si no, lo construye con
// `ow build installer --mode minimal`.

import { spawn, spawnSync } from 'node:child_process'
import * as fs from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.dirname(fileURLToPath(import.meta.url))
const REPO = path.resolve(ROOT, '..', '..')
const isWin = process.platform === 'win32'
const INSTALLER = path.join(ROOT, 'release', isWin ? 'owear-example-drawio-installer.exe' : 'owear-example-drawio-installer')
const UI_DIR = path.join(ROOT, 'installer')
const CLI = path.join(REPO, 'packages', 'cli', 'src', 'ow.js')
const PORT = 5173

function log(m) {
  console.log(`[drawio/dev-installer] ${m}`)
}
function die(m) {
  console.error(`[drawio/dev-installer] ✗ ${m}`)
  process.exit(1)
}

async function waitForServer(url, timeoutMs = 30000) {
  const start = Date.now()
  while (Date.now() - start < timeoutMs) {
    try {
      const r = await fetch(url)
      if (r.ok) return true
    } catch {
      /* aún no */
    }
    await new Promise((r) => setTimeout(r, 250))
  }
  return false
}

// 1) binario instalador (si falta, construirlo con el comando)
if (!fs.existsSync(INSTALLER)) {
  log('no hay instalador; construyendo con `ow build installer --mode minimal`…')
  const r = spawnSync(process.execPath, [CLI, 'build', 'installer', '--mode', 'minimal'], { cwd: ROOT, stdio: 'inherit' })
  if (r.status !== 0) die('falló la construcción del instalador')
}

// 2) vite dev del UI del instalador
log(`vite dev en http://localhost:${PORT} …`)
const vite = spawn('npx', ['vite', '--port', String(PORT), '--strictPort'], {
  cwd: UI_DIR,
  stdio: 'inherit',
  shell: isWin,
})
const up = await waitForServer(`http://localhost:${PORT}/`)
if (!up) {
  vite.kill()
  die('vite no arrancó')
}
log('dev server listo. Abriendo el instalador (hot-reload)…')

// 3) el binario instalador apuntando al dev server
const kernel = spawn(INSTALLER, [], {
  stdio: 'inherit',
  env: {
    ...process.env,
    OW_DEV_SERVER_URL: `http://localhost:${PORT}/`,
    GDK_BACKEND: process.env.GDK_BACKEND ?? 'x11',
  },
})
kernel.on('exit', () => {
  vite.kill('SIGTERM')
  process.exit(0)
})
process.on('SIGINT', () => {
  kernel.kill('SIGTERM')
  vite.kill('SIGTERM')
  process.exit(0)
})
