#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// ─────────────────────────────────────────────────────────────────────────────
// tools/pack-runtime.mjs — ensambla el paquete npm de runtime (kernel + módulos
// stock + headers) para la plataforma actual, listo para `npm publish`.
//
//   node tools/pack-runtime.mjs            # target por defecto (host)
//
// Deja en packages/runtime-<target>/:
//   bin/owear          binario del kernel (chmod +x)
//   bin/modules/*.so   módulos stock (.owm) junto al binario → <exe_dir>/modules
//   include/**         headers públicos para `owear-build-native`
//   LICENSE
//
// Los artefactos están git-ignored: se generan antes de publicar (prepack).
// ─────────────────────────────────────────────────────────────────────────────

import * as fs from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

const TARGETS = {
  'linux-x64-gnu': { preset: 'linux-release', pkg: 'runtime-linux-x64-gnu', exe: 'owear', ext: '.so' },
  'win32-x64': { preset: 'windows-release', pkg: 'runtime-win32-x64', exe: 'owear.exe', ext: '.dll' },
  'darwin-arm64': { preset: 'macos-release', pkg: 'runtime-darwin-arm64', exe: 'owear', ext: '.dylib' },
  'darwin-x64': { preset: 'macos-release', pkg: 'runtime-darwin-x64', exe: 'owear', ext: '.dylib' },
}

function hostTarget() {
  if (process.platform === 'linux' && process.arch === 'x64') return 'linux-x64-gnu'
  if (process.platform === 'win32' && process.arch === 'x64') return 'win32-x64'
  if (process.platform === 'darwin') return `darwin-${process.arch}`
  return null
}

function die(m) {
  console.error(`[pack-runtime] ✗ ${m}`)
  process.exit(1)
}

const argv = process.argv.slice(2)
const targetArg = argv[argv.indexOf('--target') + 1]
const target = argv.includes('--target') ? targetArg : hostTarget()
if (!target || !TARGETS[target]) die(`target desconocido: ${target ?? '(sin host)'}`)
const T = TARGETS[target]

const BUILD = path.join(ROOT, 'build', T.preset)
const PKG = path.join(ROOT, 'packages', T.pkg)
const binCandidates = [
  path.join(BUILD, 'src', T.exe),
  path.join(BUILD, 'src', 'Release', T.exe), // MSVC multi-config
  path.join(BUILD, 'src', 'Debug', T.exe),
]
const binSrc = binCandidates.find((p) => fs.existsSync(p))
if (!binSrc) {
  die(`falta el binario en ${path.relative(ROOT, path.join(BUILD, 'src'))} — compila primero:\n    cmake --preset ${T.preset} && cmake --build --preset ${T.preset}`)
}
if (!fs.existsSync(path.join(PKG, 'package.json'))) die(`no existe packages/${T.pkg}/package.json`)

// limpieza
for (const d of ['bin', 'include']) fs.rmSync(path.join(PKG, d), { recursive: true, force: true })

// kernel
const binDst = path.join(PKG, 'bin', T.exe)
fs.mkdirSync(path.dirname(binDst), { recursive: true })
fs.copyFileSync(binSrc, binDst)
if (process.platform !== 'win32') fs.chmodSync(binDst, 0o755)

// módulos stock, planos junto al binario → el kernel los busca en <exe_dir>/modules
const apiDir = path.join(BUILD, 'api')
let mods = 0
if (fs.existsSync(apiDir)) {
  const found = []
  const walk = (dir) => {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      const p = path.join(dir, e.name)
      if (e.isDirectory()) {
        if (e.name === 'CMakeFiles') continue
        walk(p)
      } else if (e.name.endsWith(T.ext) || e.name.endsWith('.owm')) {
        found.push(p)
      }
    }
  }
  walk(apiDir)
  if (found.length) fs.mkdirSync(path.join(PKG, 'bin', 'modules'), { recursive: true })
  for (const p of found) {
    fs.copyFileSync(p, path.join(PKG, 'bin', 'modules', path.basename(p)))
    mods++
  }
}

// headers públicos (para owear-build-native en apps)
const incSrc = path.join(ROOT, 'include')
if (fs.existsSync(incSrc)) fs.cpSync(incSrc, path.join(PKG, 'include'), { recursive: true })

// licencia
const lic = path.join(ROOT, 'LICENSE')
if (fs.existsSync(lic)) fs.copyFileSync(lic, path.join(PKG, 'LICENSE'))

const size = (p) => {
  let n = 0
  for (const e of fs.readdirSync(p, { withFileTypes: true })) {
    const q = path.join(p, e.name)
    n += e.isDirectory() ? size(q) : fs.statSync(q).size
  }
  return n
}
console.log(
  `[pack-runtime] ${T.pkg}: bin/${T.exe} + ${mods} módulos + include/ ` +
    `(${(size(PKG) / 1024 / 1024).toFixed(1)} MB)`
)
console.log(`[pack-runtime] listo para: cd packages/${T.pkg} && npm publish --access public`)
