#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// tools/owear-msi.mjs — genera un .MSI de la app (D1) desde un stage dir.
//
// Genera un fuente WiX v3 y lo compila con `wixl` (msitools, Linux) o
// `wix`/`candle`+`light` (Windows). Si no hay toolchain, deja el .wxs listo.
//
// Uso:
//   node tools/owear-msi.mjs --stage <dir> --out <x.msi> --name <slug> \
//     --version <v> [--publisher <p>] [--appId <id>]
//
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as crypto from 'node:crypto'
import { spawnSync } from 'node:child_process'

function die(m) {
  console.error(`[owear-msi] ✗ ${m}`)
  process.exit(1)
}
const argv = process.argv.slice(2)
const arg = (n, def = null) => (argv.includes(n) ? argv[argv.indexOf(n) + 1] : def)
const stage = arg('--stage')
const out = arg('--out')
const name = arg('--name', 'app')
const version = arg('--version', '0.0.0')
const publisher = arg('--publisher', 'unknown')
const appId = arg('--appId', name)
if (!stage || !out) die('uso: --stage <dir> --out <x.msi> --name <slug> --version <v>')
if (!fs.existsSync(stage)) die(`no existe el stage: ${stage}`)

/** GUID estable a partir de un string (UpgradeCode). */
function guidFrom(seed) {
  const h = crypto.createHash('sha1').update(seed).digest()
  const b = Buffer.from(h.subarray(0, 16))
  b[6] = (b[6] & 0x0f) | 0x40 // version 4
  b[8] = (b[8] & 0x3f) | 0x80 // variant
  const s = b.toString('hex')
  return `${s.slice(0, 8)}-${s.slice(8, 12)}-${s.slice(12, 16)}-${s.slice(16, 20)}-${s.slice(20, 32)}`.toUpperCase()
}

const xmlEsc = (s) =>
  String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;')

// ── construye el árbol WiX ──────────────────────────────────────────────────
let idSeq = 0
const nextId = (p) => `${p}${++idSeq}`

function buildTree(dir, dirId, indent) {
  let xml = ''
  for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    const p = path.join(dir, e.name)
    if (e.isDirectory()) {
      const childId = nextId('dir')
      xml += `${indent}<Directory Id="${childId}" Name="${xmlEsc(e.name)}">\n`
      xml += buildTree(p, childId, indent + '  ')
      xml += `${indent}</Directory>\n`
    } else if (e.isFile()) {
      const compId = nextId('cmp')
      const fileId = nextId('fil')
      xml +=
        `${indent}<Component Id="${compId}" Guid="*">\n` +
        `${indent}  <File Id="${fileId}" Source="${xmlEsc(p)}" KeyPath="yes" />\n` +
        `${indent}</Component>\n`
    }
  }
  return xml
}

const wxsPath = out.replace(/\.msi$/, '.wxs')
const wxs =
  `<?xml version="1.0" encoding="UTF-8"?>\n` +
  `<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">\n` +
  `  <Product Id="*" Name="${xmlEsc(name)}" Language="1033" Version="${xmlEsc(version)}"\n` +
  `           Manufacturer="${xmlEsc(publisher)}" UpgradeCode="${guidFrom(appId)}">\n` +
  `    <Package InstallerVersion="500" Compressed="yes" InstallScope="perUser" />\n` +
  `    <MajorUpgrade DowngradeErrorMessage="Ya hay una version mas reciente instalada." />\n` +
  `    <MediaTemplate EmbedCab="yes" />\n` +
  `    <Directory Id="TARGETDIR" Name="SourceDir">\n` +
  `      <Directory Id="LocalAppDataFolder">\n` +
  `        <Directory Id="INSTALLFOLDER" Name="${xmlEsc(name)}">\n` +
  buildTree(stage, 'INSTALLFOLDER', '          ') +
  `        </Directory>\n` +
  `      </Directory>\n` +
  `    </Directory>\n` +
  `  </Product>\n` +
  `</Wix>\n`

fs.mkdirSync(path.dirname(wxsPath), { recursive: true })
fs.writeFileSync(wxsPath, wxs)

const has = (cmd) => spawnSync('sh', ['-c', `command -v ${cmd}`], { stdio: 'ignore' }).status === 0

let ok = false
if (has('wixl')) {
  const r = spawnSync('wixl', ['-o', out, wxsPath], { stdio: 'inherit' })
  ok = r.status === 0
} else if (has('wix')) {
  const r = spawnSync('wix', ['build', wxsPath, '-o', out], { stdio: 'inherit' })
  ok = r.status === 0
}

if (!ok) {
  console.error(
    `[owear-msi] sin toolchain (wixl/wix). Fuente WiX lista en: ${wxsPath}\n` +
      `  · Linux: sudo apt install msitools   → wixl -o "${out}" "${wxsPath}"\n` +
      `  · Windows: dotnet tool install --global wix → wix build "${wxsPath}" -o "${out}"`,
  )
  process.exit(0) // no es un error fatal: el .wxs queda generado
}
console.log(`[owear-msi] MSI: ${out}`)
