#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// ─────────────────────────────────────────────────────────────────────────────
// tools/gen-apis.mjs — genera el "pegamento" desde los manifiestos de API.
//
// Fuente única de verdad: api/<nombre>/owear.module.json
//
// Genera (NO editar a mano; este script los reescribe):
//   · api/generated.cmake               → add_subdirectory de cada módulo (.owm)
//   · src/Core/builtins.generated.cmake → fuentes de builtins por plataforma
//   · src/Core/BuiltinRegistry.generated.cpp → registro de builtins en el Dispatcher
//
// Uso:
//   node tools/gen-apis.mjs           # regenera
//   node tools/gen-apis.mjs --check   # falla si los generados están desactualizados (CI)
// ─────────────────────────────────────────────────────────────────────────────

import * as fs from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const API_DIR = path.join(ROOT, 'api')
const PLATFORMS = ['linux', 'win', 'mac']
const GUARD = { linux: 'defined(OW_BUILTINS_GTK)', win: 'defined(OW_PLATFORM_WIN)', mac: 'defined(__APPLE__)' }
const CHECK = process.argv.includes('--check')

const HEADER = (what) =>
  `# ─────────────────────────────────────────────────────────────────────────────\n` +
  `# GENERADO por tools/gen-apis.mjs — NO EDITAR A MANO.\n` +
  `# Fuente de verdad: api/<nombre>/owear.module.json (${what})\n` +
  `# Regenerar: node tools/gen-apis.mjs\n` +
  `# ─────────────────────────────────────────────────────────────────────────────\n`

const CPP_HEADER =
  `// ─────────────────────────────────────────────────────────────────────────────\n` +
  `// GENERADO por tools/gen-apis.mjs — NO EDITAR A MANO.\n` +
  `// Fuente de verdad: api/<nombre>/owear.module.json (kind=builtin)\n` +
  `// Regenerar: node tools/gen-apis.mjs\n` +
  `// ─────────────────────────────────────────────────────────────────────────────\n`

function fail(msg) {
  console.error(`[gen-apis] ✗ ${msg}`)
  process.exit(1)
}

function readManifests() {
  const manifests = []
  for (const entry of fs.readdirSync(API_DIR, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    if (!entry.isDirectory()) continue
    const file = path.join(API_DIR, entry.name, 'owear.module.json')
    if (!fs.existsSync(file)) fail(`api/${entry.name}/ no tiene owear.module.json`)
    let m
    try {
      m = JSON.parse(fs.readFileSync(file, 'utf8'))
    } catch (e) {
      fail(`api/${entry.name}/owear.module.json inválido: ${e.message}`)
    }
    if (m.name !== entry.name) fail(`api/${entry.name}/: "name" (${m.name}) != carpeta (${entry.name})`)
    if (!['module', 'builtin'].includes(m.kind)) fail(`${m.name}: "kind" inválido (${m.kind})`)
    if (!m.version || !/^\d+\.\d+\.\d+$/.test(m.version)) fail(`${m.name}: "version" inválida`)
    const plats = m.kind === 'builtin' && m.descriptors
      ? [...new Set(m.descriptors.flatMap((d) => d.platforms ?? m.platforms ?? PLATFORMS))]
      : m.platforms ?? PLATFORMS
    for (const p of plats) if (!PLATFORMS.includes(p)) fail(`${m.name}: plataforma desconocida "${p}"`)
    manifests.push({ ...m, dir: entry.name })
  }
  return manifests
}

function genApiCmake(manifests) {
  const modules = manifests.filter((m) => m.kind === 'module').map((m) => m.name)
  const lines = [HEADER('kind=module'), `# ${modules.length} módulos .owm`, '']
  for (const name of modules) lines.push(`add_subdirectory(${name})`)
  return lines.join('\n') + '\n'
}

function builtinSourcesByPlatform(manifests) {
  const byPlat = { linux: [], win: [], mac: [] }
  for (const m of manifests.filter((m) => m.kind === 'builtin')) {
    for (const [plat, srcs] of Object.entries(m.sources ?? {})) {
      if (!byPlat[plat]) fail(`${m.name}: sources de plataforma desconocida "${plat}"`)
      for (const src of srcs) byPlat[plat].push(`api/${m.name}/${src}`)
    }
  }
  return byPlat
}

function genBuiltinsCmake(manifests) {
  const byPlat = builtinSourcesByPlatform(manifests)
  const lines = [HEADER('kind=builtin'), '']
  for (const plat of PLATFORMS) {
    const srcs = byPlat[plat]
    if (!srcs.length) {
      lines.push(`set(OW_BUILTIN_SOURCES_${plat})`)
    } else {
      lines.push(`set(OW_BUILTIN_SOURCES_${plat}`)
      for (const s of srcs) lines.push(`    \${CMAKE_SOURCE_DIR}/${s}`)
      lines[lines.length - 1] += ')'
    }
    lines.push('')
  }
  lines.push(`# Selección por plataforma (OW_PLATFORM: linux|win|mac).`)
  lines.push('set(OW_BUILTIN_SOURCES ${OW_BUILTIN_SOURCES_${OW_PLATFORM}})')
  return lines.join('\n') + '\n'
}

function guardsFor(platforms) {
  const g = [...new Set((platforms ?? PLATFORMS).map((p) => GUARD[p]))]
  if (g.length === PLATFORMS.length) return null // todas → sin guard
  return g
}

function genBuiltinRegistry(manifests) {
  const descs = []
  for (const m of manifests.filter((m) => m.kind === 'builtin')) {
    for (const d of m.descriptors ?? []) descs.push({ ...d, api: m.name })
  }

  const factories = new Map() // factory -> guards|null
  for (const d of descs) {
    factories.set(d.factory, d.core ? null : guardsFor(d.platforms))
  }

  const out = [CPP_HEADER, '#include "../Bridge/Dispatcher.hpp"', '#include "ow_api.h"', '', 'namespace ow::internal {', '']
  out.push('// Factories declaradas en src/Core/… o api/…/src/….')
  for (const [factory, guards] of factories) {
    if (guards) out.push(`#if ${guards.join(' || ')}`)
    out.push(`const ow_module_desc_t* ${factory}();`)
    if (guards) out.push('#endif')
  }
  out.push('')
  out.push('void RegisterGeneratedBuiltins() {')
  for (const d of descs) {
    const guards = d.core ? null : guardsFor(d.platforms)
    if (guards) out.push(`#if ${guards.join(' || ')}`)
    out.push(`    Dispatcher::Get().RegisterModule(${d.factory}(), "builtin:${d.name}");`)
    if (guards) out.push('#endif')
  }
  out.push('}')
  out.push('')
  out.push('} // namespace ow::internal')
  return out.join('\n') + '\n'
}

function writeOrCheck(file, content) {
  const rel = path.relative(ROOT, file)
  if (CHECK) {
    const existing = fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : ''
    if (existing !== content) {
      console.error(`[gen-apis] ✗ desactualizado: ${rel} — corre: node tools/gen-apis.mjs`)
      process.exitCode = 1
    } else {
      console.log(`[gen-apis] ✓ ${rel}`)
    }
    return
  }
  fs.mkdirSync(path.dirname(file), { recursive: true })
  fs.writeFileSync(file, content)
  console.log(`[gen-apis] ${rel} actualizado`)
}

function main() {
  const manifests = readManifests()
  writeOrCheck(path.join(API_DIR, 'generated.cmake'), genApiCmake(manifests))
  writeOrCheck(path.join(ROOT, 'src', 'Core', 'builtins.generated.cmake'), genBuiltinsCmake(manifests))
  writeOrCheck(path.join(ROOT, 'src', 'Core', 'BuiltinRegistry.generated.cpp'), genBuiltinRegistry(manifests))
  if (!CHECK) {
    const mods = manifests.filter((m) => m.kind === 'module').length
    const built = manifests.filter((m) => m.kind === 'builtin').length
    console.log(`[gen-apis] ${manifests.length} APIs (${mods} módulos, ${built} builtins)`)
  }
}

main()
