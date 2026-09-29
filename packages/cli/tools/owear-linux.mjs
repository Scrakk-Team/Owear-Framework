#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// tools/owear-linux.mjs — empaquetado Linux de la app (D1).
//
//   deb        → .deb (ar + control.tar.gz + data.tar.gz), sin deps externas.
//   appimage   → AppDir + appimagetool (si está en PATH).
//
// Uso:
//   node tools/owear-linux.mjs deb --stage <dataRoot> --out <x.deb> \
//     --name <slug> --version <v> [--arch amd64] [--maintainer <m>] [--description <d>]
//   node tools/owear-linux.mjs appimage --appdir <AppDir> --out <x.AppImage>
//
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as zlib from 'node:zlib'
import { spawnSync } from 'node:child_process'

function die(m) {
  console.error(`[owear-linux] ✗ ${m}`)
  process.exit(1)
}
const argv = process.argv.slice(2)
const cmd = argv[0]
const arg = (n, def = null) => (argv.includes(n) ? argv[argv.indexOf(n) + 1] : def)

// ── tar (ustar) + gzip ──────────────────────────────────────────────────────
function oct(n, len) {
  return n.toString(8).padStart(len - 1, '0') + '\0'
}
function tarHeader(name, size, mode, typeflag, mtime = 0) {
  const h = Buffer.alloc(512, 0)
  const nm = name.length > 100 ? name.slice(0, 100) : name
  h.write(nm, 0, 100, 'utf8')
  h.write(oct(mode, 8), 100)
  h.write(oct(0, 8), 108)
  h.write(oct(0, 8), 116)
  h.write(oct(size, 12), 124)
  h.write(oct(mtime, 12), 136)
  h.write('        ', 148)
  h.write(typeflag, 156)
  h.write('ustar\0', 257)
  h.write('00', 263)
  let sum = 0
  for (const b of h) sum += b
  h.write(sum.toString(8).padStart(6, '0') + '\0 ', 148)
  return h
}
function tarDir(dir) {
  const chunks = []
  const walk = (d, base) => {
    for (const e of fs.readdirSync(d, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
      const p = path.join(d, e.name)
      const rel = base ? `${base}/${e.name}` : e.name
      const st = fs.lstatSync(p)
      if (st.isSymbolicLink()) {
        chunks.push(tarHeader(rel, 0, 0o777, '2', Math.floor(st.mtimeMs / 1000)))
        const link = fs.readlinkSync(p)
        const b = Buffer.from(link)
        chunks.push(b)
        const pad = (512 - (b.length % 512)) % 512
        if (pad) chunks.push(Buffer.alloc(pad, 0))
      } else if (e.isDirectory()) {
        chunks.push(tarHeader(rel + '/', 0, st.mode & 0o777, '5', Math.floor(st.mtimeMs / 1000)))
        walk(p, rel)
      } else if (e.isFile()) {
        const data = fs.readFileSync(p)
        chunks.push(tarHeader(rel, data.length, st.mode & 0o777, '0', Math.floor(st.mtimeMs / 1000)), data)
        const pad = (512 - (data.length % 512)) % 512
        if (pad) chunks.push(Buffer.alloc(pad, 0))
      }
    }
  }
  walk(dir, '')
  chunks.push(Buffer.alloc(1024, 0))
  return Buffer.concat(chunks)
}
const tarGz = (dir) => zlib.gzipSync(tarDir(dir), { level: 9 })

// ── ar (formato .deb) ───────────────────────────────────────────────────────
function ar(entries) {
  const parts = [Buffer.from('!<arch>\n')]
  for (const e of entries) {
    const data = e.data
    const h = Buffer.alloc(60, 0x20)
    const name = (e.name + '/').slice(0, 16)
    h.write(name, 0)
    h.write(String(Math.floor(Date.now() / 1000)).padEnd(12), 16)
    h.write('0'.padEnd(6), 28)
    h.write('0'.padEnd(6), 34)
    h.write('100644'.padEnd(8), 40)
    h.write(String(data.length).padEnd(10), 48)
    h.write('`\n', 58)
    parts.push(h, data)
    if (data.length % 2) parts.push(Buffer.from('\n'))
  }
  return Buffer.concat(parts)
}

function buildDeb() {
  const stage = arg('--stage')
  const out = arg('--out')
  const name = arg('--name', 'app')
  const version = arg('--version', '0.0.0')
  const arch = arg('--arch', 'amd64')
  const maintainer = arg('--maintainer', 'unknown <unknown@example.com>')
  const description = arg('--description', name)
  if (!stage || !out) die('uso: deb --stage <dataRoot> --out <x.deb> --name <slug> --version <v>')
  if (!fs.existsSync(stage)) die(`no existe el stage: ${stage}`)

  const controlDir = fs.mkdtempSync('/tmp/owear-deb-')
  fs.writeFileSync(
    path.join(controlDir, 'control'),
    `Package: ${name}\nVersion: ${version}\nArchitecture: ${arch}\n` +
      `Maintainer: ${maintainer}\nDescription: ${description}\n`,
  )
  fs.writeFileSync(
    path.join(controlDir, 'postinst'),
    `#!/bin/sh\nset -e\nif command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database -q || true; fi\nexit 0\n`,
  )
  fs.chmodSync(path.join(controlDir, 'postinst'), 0o755)

  const deb = ar([
    { name: 'debian-binary', data: Buffer.from('2.0\n') },
    { name: 'control.tar.gz', data: tarGz(controlDir) },
    { name: 'data.tar.gz', data: tarGz(stage) },
  ])
  fs.mkdirSync(path.dirname(out), { recursive: true })
  fs.writeFileSync(out, deb)
  fs.rmSync(controlDir, { recursive: true, force: true })
  const mb = (deb.length / 1024 / 1024).toFixed(2)
  console.log(`[owear-linux] .deb: ${out} (${mb} MB)`)
}

function buildAppImage() {
  const appDir = arg('--appdir')
  const out = arg('--out')
  if (!appDir || !out) die('uso: appimage --appdir <AppDir> --out <x.AppImage>')
  if (!fs.existsSync(appDir)) die(`no existe el AppDir: ${appDir}`)
  const tool = ['appimagetool', 'appimagetool-x86_64.AppImage'].find((t) => {
    const r = spawnSync('sh', ['-c', `command -v ${t}`], { stdio: 'ignore' })
    return r.status === 0
  })
  if (!tool) {
    die(
      'appimagetool no está en PATH. Instálalo (https://github.com/AppImage/appimagetool) ' +
        `y reintenta. El AppDir queda listo en: ${appDir}`,
    )
  }
  const r = spawnSync(tool, [appDir, out], { stdio: 'inherit', env: { ...process.env, ARCH: arg('--arch', 'x86_64') } })
  if (r.status !== 0) die('appimagetool falló')
  console.log(`[owear-linux] AppImage: ${out}`)
}

if (cmd === 'deb') buildDeb()
else if (cmd === 'appimage') buildAppImage()
else die('uso: owear-linux <deb|appimage> …')
