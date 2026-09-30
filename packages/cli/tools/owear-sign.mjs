#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// tools/owear-sign.mjs — firma de artefactos de Owear.
//
// Dos mecanismos, combinables:
//
//   1. Authenticode (Windows PE, .exe/.msi): firma del binario con un
//      certificado (PFX/P12). Requiere `osslsigncode` (Linux/macOS) o
//      `signtool` (Windows). Da confianza de **editor** ante el SO
//      (SmartScreen, "Unknown publisher").
//
//   2. Ed25519 desprendida (cualquier plataforma): escribe `<file>.sig` con la
//      firma del fichero completo. La clave pública (misma que el updater)
//      permite verificar el artefacto de forma independiente del feed.
//
// Nunca rompe un build: si no hay material de firma o falta la herramienta,
// omite con un aviso (salvo `--require`, que lo convierte en error).
//
// Uso:
//   node tools/owear-sign.mjs --file release/MiApp \
//     [--ed25519-key owear-signing.pem] \
//     [--pfx cert.p12 --pfx-password-env OW_PFX_PASS] \
//     [--timestamp http://timestamp.digicert.com] \
//     [--name "Mi App" --url https://miapp.dev] \
//     [--require] [--dry-run]
//
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as crypto from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const C = { reset: '\x1b[0m', dim: '\x1b[2m', green: '\x1b[32m', yellow: '\x1b[33m', red: '\x1b[31m' }
const info = (m) => console.log(`${C.dim}[owear-sign]${C.reset} ${m}`)
const warn = (m) => console.warn(`${C.yellow}[owear-sign] ⚠${C.reset} ${m}`)
const fail = (m) => console.error(`${C.red}[owear-sign] ✗${C.reset} ${m}`)

const isPE = (file) => {
  try {
    const fd = fs.openSync(file, 'r')
    const b = Buffer.alloc(2)
    fs.readSync(fd, b, 0, 2, 0)
    fs.closeSync(fd)
    return b[0] === 0x4d && b[1] === 0x5a // "MZ"
  } catch {
    return false
  }
}

const PE_EXT = new Set(['.exe', '.dll', '.msi', '.sys', '.ocx', '.scr'])

/** ¿Es un PE/MSI firmable con Authenticode? (por extensión o cabecera MZ). */
export function isAuthenticodeTarget(file) {
  if (PE_EXT.has(path.extname(file).toLowerCase())) return true
  return isPE(file)
}

/** Comando de firma Authenticode según la herramienta disponible. */
export function buildAuthenticodeInvocation({ tool, file, pfx, password, timestamp, name, url }) {
  if (tool === 'osslsigncode') {
    const args = ['sign', '-pkcs12', pfx, '-in', file, '-out', `${file}.signed`]
    if (password) args.push('-pass', password)
    if (name) args.push('-n', name)
    if (url) args.push('-i', url)
    if (timestamp) args.push('-t', timestamp)
    return { cmd: 'osslsigncode', args, out: `${file}.signed`, inPlace: true }
  }
  if (tool === 'signtool') {
    const args = ['sign', '/f', pfx, '/fd', 'SHA256', '/d', name || path.basename(file)]
    if (password) args.push('/p', password)
    if (timestamp) args.push('/tr', timestamp, '/td', 'SHA256')
    args.push(file)
    return { cmd: 'signtool', args, out: file, inPlace: false }
  }
  throw new Error(`herramienta de firma desconocida: ${tool}`)
}

/** ¿Está la herramienta en el PATH? */
export function hasTool(cmd) {
  const probe = process.platform === 'win32' ? 'where' : 'which'
  return spawnSync(probe, [cmd], { stdio: 'ignore' }).status === 0
}

/** Firma Ed25519 desprendida: escribe `<file>.sig` (base64) y devuelve la firma. */
export function signEd25519(file, keyPath) {
  const data = fs.readFileSync(file)
  const key = crypto.createPrivateKey(fs.readFileSync(keyPath))
  const sig = crypto.sign(null, data, key)
  fs.writeFileSync(`${file}.sig`, sig.toString('base64') + '\n')
  return sig
}

/** Verifica una firma Ed25519 desprendida contra `pub` (PEM o SPKI base64). */
export function verifyEd25519(file, sigPath, pub) {
  const key = pub.includes('BEGIN')
    ? crypto.createPublicKey(pub)
    : crypto.createPublicKey({ key: Buffer.from(pub, 'base64'), format: 'der', type: 'spki' })
  const data = fs.readFileSync(file)
  const sig = Buffer.from(fs.readFileSync(sigPath, 'utf8').trim(), 'base64')
  return crypto.verify(null, data, key, sig)
}

/** Firma Authenticode (in-place si es posible). Devuelve 'signed'|'skipped'|'failed'. */
export function signAuthenticode({ file, pfx, password, timestamp, name, url, require = false }) {
  const tool = hasTool('osslsigncode') ? 'osslsigncode' : hasTool('signtool') ? 'signtool' : null
  if (!tool) {
    const msg = 'ni osslsigncode ni signtool están en el PATH'
    if (require) throw new Error(msg)
    warn(`${msg} — Authenticode omitido`)
    return 'skipped'
  }
  if (!pfx || !fs.existsSync(pfx)) {
    const msg = `certificado no encontrado: ${pfx ?? '(sin --pfx)'}`
    if (require) throw new Error(msg)
    warn(`${msg} — Authenticode omitido`)
    return 'skipped'
  }
  const inv = buildAuthenticodeInvocation({ tool, file, pfx, password, timestamp, name, url })
  const r = spawnSync(inv.cmd, inv.args, { stdio: 'inherit' })
  if (r.status !== 0) {
    if (require) throw new Error(`${inv.cmd} falló (${r.status})`)
    warn(`${inv.cmd} falló (${r.status})`)
    return 'failed'
  }
  if (inv.inPlace) {
    fs.renameSync(inv.out, file)
    info(`Authenticode OK (${tool})`)
  } else {
    info(`Authenticode OK (${tool})`)
  }
  return 'signed'
}

function main() {
  const argv = process.argv.slice(2)
  const arg = (n, def = null) => (argv.includes(n) ? argv[argv.indexOf(n) + 1] : def)
  const flag = (n) => argv.includes(n)

  const file = arg('--file')
  if (!file) {
    fail('--file es obligatorio')
    process.exit(1)
  }
  if (!fs.existsSync(file)) {
    fail(`no existe: ${file}`)
    process.exit(1)
  }

  const edKey = arg('--ed25519-key')
  const pfx = arg('--pfx')
  const pwEnv = arg('--pfx-password-env')
  const password = arg('--password', pwEnv ? process.env[pwEnv] ?? '' : '')
  const timestamp = arg('--timestamp')
  const name = arg('--name')
  const url = arg('--url')
  const require = flag('--require')
  const dryRun = flag('--dry-run')
  const isWin = process.platform === 'win32'

  let did = 0

  // 1) Authenticode (solo PE/MSI y solo en Windows o con osslsigncode)
  if (pfx) {
    if (!isAuthenticodeTarget(file)) {
      info(`${path.basename(file)} no es PE/MSI — Authenticode no aplica`)
    } else if (dryRun) {
      const tool = hasTool('osslsigncode') ? 'osslsigncode' : 'signtool'
      const inv = buildAuthenticodeInvocation({ tool, file, pfx, password, timestamp, name, url })
      console.log(JSON.stringify({ authenticode: { cmd: inv.cmd, args: inv.args } }))
      did++
    } else {
      if (signAuthenticode({ file, pfx, password, timestamp, name, url, require }) === 'signed') did++
    }
  }

  // 2) Ed25519 desprendida (siempre disponible)
  if (edKey) {
    if (!fs.existsSync(edKey)) {
      fail(`clave no encontrada: ${edKey}`)
      process.exit(1)
    }
    if (dryRun) {
      console.log(JSON.stringify({ ed25519: { file, key: edKey, out: `${file}.sig` } }))
    } else {
      signEd25519(file, edKey)
      info(`Ed25519 OK → ${path.basename(file)}.sig`)
      did++
    }
  }

  if (!did) {
    if (require) {
      fail('no se firmó nada (--require)')
      process.exit(1)
    }
    warn('sin material de firma (--ed25519-key / --pfx) — nada que hacer')
  }
  void isWin
}

// Ejecuta main solo como script (permite importarlo en tests).
if (process.argv[1] && fileURLToPath(import.meta.url) === path.resolve(process.argv[1])) main()
