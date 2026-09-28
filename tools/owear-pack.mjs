#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// tools/owear-pack.mjs — empaqueta un directorio (bundle) DENTRO del binario del
// kernel → single binary. Formato:
//   [ kernel ][ payload.tar.gz ][ "OWPK1\0\0\0" + offset(u64) + size(u64) ]
//
//   node tools/owear-pack.mjs --kernel <owear> --in <bundleDir> --out <MiApp>
//
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as zlib from 'node:zlib'

function die(m) {
  console.error(`[owear-pack] ✗ ${m}`)
  process.exit(1)
}
const argv = process.argv.slice(2)
const arg = (n) => (argv.includes(n) ? argv[argv.indexOf(n) + 1] : null)
const kernel = arg('--kernel')
const inDir = arg('--in')
const out = arg('--out')
if (!kernel || !inDir || !out) die('uso: --kernel <owear> --in <bundle> --out <MiApp>')

// ── tar (ustar) mínimo ──────────────────────────────────────────────────────
function oct(n, len) {
  return n.toString(8).padStart(len - 1, '0') + '\0'
}
function tarHeader(name, size, mode, typeflag) {
  const h = Buffer.alloc(512, 0)
  h.write(name.slice(0, 100), 0, 100, 'utf8')
  h.write(oct(mode, 8), 100)
  h.write(oct(0, 8), 108) // uid
  h.write(oct(0, 8), 116) // gid
  h.write(oct(size, 12), 124)
  h.write(oct(0, 12), 136) // mtime
  h.write('        ', 148) // checksum (espacios)
  h.write(typeflag, 156)
  h.write('ustar\0', 257)
  h.write('00', 263)
  let sum = 0
  for (const b of h) sum += b
  h.write(sum.toString(8).padStart(6, '0') + '\0 ', 148)
  return h
}
function walk(dir, base = '') {
  const out = []
  for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    const p = path.join(dir, e.name)
    const rel = base ? `${base}/${e.name}` : e.name
    if (e.isDirectory()) {
      out.push({ name: rel + '/', dir: true })
      out.push(...walk(p, rel))
    } else if (e.isFile()) {
      out.push({ name: rel, dir: false, path: p })
    }
  }
  return out
}
function makeTar(dir) {
  const chunks = []
  for (const e of walk(dir)) {
    if (e.dir) {
      chunks.push(tarHeader(e.name, 0, 0o755, '5'))
    } else {
      const data = fs.readFileSync(e.path)
      chunks.push(tarHeader(e.name, data.length, 0o644, '0'), data)
      const pad = (512 - (data.length % 512)) % 512
      if (pad) chunks.push(Buffer.alloc(pad, 0))
    }
  }
  chunks.push(Buffer.alloc(1024, 0)) // 2 bloques fin
  return Buffer.concat(chunks)
}

// ── ensambla ────────────────────────────────────────────────────────────────
if (!fs.existsSync(kernel)) die(`no existe el kernel: ${kernel}`)
if (!fs.existsSync(inDir)) die(`no existe el bundle: ${inDir}`)

const payload = zlib.gzipSync(makeTar(inDir), { level: 9 })
const ker = fs.readFileSync(kernel)
const footer = Buffer.alloc(24)
footer.write('OWPK1\0\0\0', 0, 'binary')
footer.writeBigUInt64LE(BigInt(ker.length), 8)
footer.writeBigUInt64LE(BigInt(payload.length), 16)
fs.writeFileSync(out, Buffer.concat([ker, payload, footer]))
fs.chmodSync(out, 0o755)

const mb = (b) => (b / 1024 / 1024).toFixed(2)
console.log(
  `[owear-pack] ${out}  kernel=${mb(ker.length)}MB + payload=${mb(payload.length)}MB (${fs.readdirSync(inDir).length} items)`,
)
