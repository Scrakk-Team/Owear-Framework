// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// tools/version.mjs — versión única del monorepo.
//
//   node tools/version.mjs            → sincroniza packages/* a la versión de la raíz
//   node tools/version.mjs set X.Y.Z  → fija raíz + packages a X.Y.Z
//   node tools/version.mjs check      → falla si hay desincronía o falta el changelog
//
// La versión de la raíz (package.json) es la fuente de verdad. Cada release debe
// tener su docs/changelog/changelog-<versión>.md.
//

import * as fs from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const ROOT_PKG = path.join(ROOT, 'package.json')
const CHANGELOG_DIR = path.join(ROOT, 'docs', 'changelog')

const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8'))
const writeJson = (p, o) => fs.writeFileSync(p, JSON.stringify(o, null, 2) + '\n')

function packageFiles() {
  const dir = path.join(ROOT, 'packages')
  return fs
    .readdirSync(dir, { withFileTypes: true })
    .filter((d) => d.isDirectory() && fs.existsSync(path.join(dir, d.name, 'package.json')))
    .map((d) => path.join(dir, d.name, 'package.json'))
}

function changelogPath(v) {
  return path.join(CHANGELOG_DIR, `changelog-${v}.md`)
}

const [cmd, arg] = process.argv.slice(2)

if (cmd === 'set') {
  if (!arg || !/^\d+\.\d+\.\d+$/.test(arg)) {
    console.error('uso: node tools/version.mjs set X.Y.Z')
    process.exit(1)
  }
  const root = readJson(ROOT_PKG)
  root.version = arg
  writeJson(ROOT_PKG, root)
  for (const p of packageFiles()) {
    const pkg = readJson(p)
    pkg.version = arg
    writeJson(p, pkg)
  }
  console.log(`[version] = ${arg} (raíz + ${packageFiles().length} paquetes)`)
  if (!fs.existsSync(changelogPath(arg))) {
    console.warn(`[version] ⚠ falta docs/changelog/changelog-${arg}.md — créalo antes de publicar`)
  }
} else if (cmd === 'check') {
  const v = readJson(ROOT_PKG).version
  const bad = []
  for (const p of packageFiles()) {
    const pkg = readJson(p)
    if (pkg.version !== v) bad.push(`${path.relative(ROOT, p)} → ${pkg.version}`)
  }
  if (bad.length) {
    console.error(`[version] ✗ desincronizados con la raíz (${v}):\n  ${bad.join('\n  ')}`)
    process.exit(1)
  }
  if (!fs.existsSync(changelogPath(v))) {
    console.error(`[version] ✗ falta docs/changelog/changelog-${v}.md`)
    process.exit(1)
  }
  console.log(`[version] ✓ ${v} sincronizada y con changelog`)
} else {
  // sync packages a la versión de la raíz
  const v = readJson(ROOT_PKG).version
  for (const p of packageFiles()) {
    const pkg = readJson(p)
    if (pkg.version !== v) {
      pkg.version = v
      writeJson(p, pkg)
      console.log(`[version] ${path.relative(ROOT, p)} → ${v}`)
    }
  }
  console.log(`[version] sincronizado a ${v}`)
}
