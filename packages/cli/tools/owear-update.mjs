#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// tools/owear-update.mjs — publica un update de Owear: calcula el blockmap
// (delta por bloques), genera el manifiesto YAML del canal y lo firma con
// Ed25519.
//
// Salidas (en --out-dir, por defecto junto al artefacto):
//   <file>.blockmap   { blockSize, size, blocks:[sha256…] }
//   <channel>.yml     manifiesto YAML (version, sha256, sha512, blockSize,
//                     blockmap, path, releaseDate, notes, signature)
//
// El SDK (autoUpdater) verifica `signature` con la clave pública del bridge
// (Ed25519 sobre "<version>:<sha256>") y descarga solo los bloques cambiados.
//
// Uso:
//   node tools/owear-update.mjs --file release/MiApp-1.4.0 --version 1.4.0 \
//     --channel latest --url https://up.miapp.dev --key private.pem \
//     [--notes "…"|--notes @notes.md] [--block-size 262144] [--mandatory] \
//     [--out-dir release]
//
//   # claves (una vez):
//   node tools/owear-update.mjs --gen-key ./owear-signing
//       → owear-signing.pem (privada, Ed25519 PKCS8)
//         owear-signing.pub.b64 (pública SPKI base64 — va en el bridge)
//
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as crypto from 'node:crypto'

function die(m) {
  console.error(`[owear-update] ✗ ${m}`)
  process.exit(1)
}
const argv = process.argv.slice(2)
const arg = (n, def = null) => (argv.includes(n) ? argv[argv.indexOf(n) + 1] : def)
const flag = (n) => argv.includes(n)

// ── generación de claves ────────────────────────────────────────────────────
const genKey = arg('--gen-key')
if (genKey) {
  if (fs.existsSync(`${genKey}.pem`)) die(`ya existe ${genKey}.pem`)
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519')
  fs.writeFileSync(`${genKey}.pem`, privateKey.export({ format: 'pem', type: 'pkcs8' }), { mode: 0o600 })
  fs.writeFileSync(`${genKey}.pub.b64`, publicKey.export({ format: 'der', type: 'spki' }).toString('base64'))
  console.log(`[owear-update] ✓ clave privada: ${genKey}.pem`)
  console.log(`[owear-update] ✓ clave pública: ${genKey}.pub.b64 (ponla en el bridge: updater.publicKey)`)
  process.exit(0)
}

// ── validación de argumentos ────────────────────────────────────────────────
const file = arg('--file')
const version = arg('--version')
if (!file || !version) die('--file y --version son obligatorios (o usa --gen-key)')
if (!fs.existsSync(file)) die(`no existe el artefacto: ${file}`)
if (!/^\d+\.\d+\.\d+/.test(version)) die(`versión inválida: ${version} (semver esperado)`)

const channel = arg('--channel', 'latest')
const blockSize = parseInt(arg('--block-size', '262144'), 10)
if (!Number.isFinite(blockSize) || blockSize <= 0) die('--block-size inválido')
const baseUrl = (arg('--url', '') || '').replace(/\/+$/, '')
const outDir = arg('--out-dir', path.dirname(file))
const keyPath = arg('--key')
const mandatory = flag('--mandatory')

let notes = arg('--notes', '')
if (notes.startsWith('@')) {
  const nf = notes.slice(1)
  if (!fs.existsSync(nf)) die(`no existe el fichero de notas: ${nf}`)
  notes = fs.readFileSync(nf, 'utf8').trimEnd()
}

// ── integridad + blockmap ───────────────────────────────────────────────────
const data = fs.readFileSync(file)
const sha256 = crypto.createHash('sha256').update(data).digest('hex')
const sha512 = crypto.createHash('sha512').update(data).digest('hex')

const blocks = []
for (let off = 0; off < data.length; off += blockSize) {
  blocks.push(crypto.createHash('sha256').update(data.subarray(off, Math.min(off + blockSize, data.length))).digest('hex'))
}
if (data.length === 0) blocks.push(crypto.createHash('sha256').update(Buffer.alloc(0)).digest('hex'))

fs.mkdirSync(outDir, { recursive: true })
const artifactName = path.basename(file)
const blockmapPath = path.join(outDir, `${artifactName}.blockmap`)
fs.writeFileSync(blockmapPath, JSON.stringify({ blockSize, size: data.length, sha256, blocks }))

// ── firma Ed25519 (del manifiesto y del binario) ────────────────────────────
//   signature:  sobre "<version>:<sha256>" (autentica metadata)
//   binarySig:  sobre los bytes del artefacto (autentica el payload)
let signature = ''
let binarySig = ''
if (keyPath) {
  if (!fs.existsSync(keyPath)) die(`no existe la clave: ${keyPath}`)
  const privateKey = crypto.createPrivateKey(fs.readFileSync(keyPath))
  signature = crypto
    .sign(null, Buffer.from(`${version}:${sha256}`), privateKey)
    .toString('base64')
  binarySig = crypto.sign(null, data, privateKey).toString('base64')
}

// ── manifiesto YAML ─────────────────────────────────────────────────────────
const blockmapUrl = baseUrl ? `${baseUrl}/${path.basename(blockmapPath)}` : path.basename(blockmapPath)
const releaseDate = new Date().toISOString()
const yamlEscape = (s) => (/[:#'"@|>{}\[\],&*!?%`-]|^\s|\s$|\n/.test(s) ? `'${s.replace(/'/g, "''")}'` : s)
// las notas van como bloque literal
const notesBlock = notes
  ? `notes: |\n${notes
      .split('\n')
      .map((l) => `  ${l}`)
      .join('\n')}\n`
  : ''

const manifest = [
  `version: ${version}`,
  `releaseDate: '${releaseDate}'`,
  `path: ${yamlEscape(artifactName)}`,
  `sha256: ${sha256}`,
  `sha512: ${sha512}`,
  `size: ${data.length}`,
  `blockSize: ${blockSize}`,
  `blockmap: ${yamlEscape(blockmapUrl)}`,
  mandatory ? 'mandatory: true' : null,
  signature ? `signature: ${signature}` : null,
  binarySig ? `binarySig: ${binarySig}` : null,
  notesBlock || null,
  '',
]
  .filter((l) => l !== null)
  .join('\n')

const manifestPath = path.join(outDir, `${channel}.yml`)
fs.writeFileSync(manifestPath, manifest)

// ── resumen ─────────────────────────────────────────────────────────────────
const mb = (n) => (n / 1024 / 1024).toFixed(1)
console.log(`[owear-update] ✓ artefacto  ${artifactName}  ${mb(data.length)} MB`)
console.log(`[owear-update] ✓ blockmap   ${path.basename(blockmapPath)}  (${blocks.length} bloques × ${(blockSize / 1024).toFixed(0)} KiB)`)
console.log(`[owear-update] ✓ manifiesto ${manifestPath}`)
console.log(`[owear-update] ${signature ? '✓ firmado' : '⚠ sin firma (--key)'}  sha256=${sha256.slice(0, 16)}…`)
if (baseUrl) {
  console.log(`[owear-update] feed: ${baseUrl}`)
  console.log(`[owear-update]   ${baseUrl}/${channel}.yml`)
  console.log(`[owear-update]   ${baseUrl}/${artifactName}`)
  console.log(`[owear-update]   ${baseUrl}/${path.basename(blockmapPath)}`)
}
