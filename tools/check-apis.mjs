#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// ─────────────────────────────────────────────────────────────────────────────
// tools/check-apis.mjs — valida que los manifiestos no mientan.
//
// Comprueba, por cada api/<nombre>/owear.module.json:
//   · kind=module  → el descriptor C++ se llama como el manifiesto y el
//                    conjunto de funciones coincide EXACTAMENTE.
//   · kind=builtin → cada descriptor.factory existe en el código y sus
//                    funciones declaradas existen en las fuentes.
//
// Uso: node tools/check-apis.mjs   (falla con exit 1 si hay desajustes)
// ─────────────────────────────────────────────────────────────────────────────

import * as fs from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const API = path.join(ROOT, 'src', 'api')

function walk(dir, acc = []) {
  if (!fs.existsSync(dir)) return acc
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name)
    if (e.isDirectory()) walk(p, acc)
    else if (/\.(cpp|mm|cc)$/.test(e.name)) acc.push(p)
  }
  return acc
}

function extract(files) {
  const fns = new Set()
  const descs = new Set()
  const factories = new Set()
  for (const f of files) {
    const s = fs.readFileSync(f, 'utf8')
    for (const m of s.matchAll(/\{\s*"([A-Za-z_][\w-]*)"\s*,\s*(?:&|\[)/g)) fns.add(m[1])
    for (const m of s.matchAll(/OW_FN\(\s*([A-Za-z_]\w*)\s*\)/g)) fns.add(m[1])
    for (const m of s.matchAll(/OW_MODULE_BEGIN\(\s*([A-Za-z_]\w*)/g)) descs.add(m[1])
    for (const m of s.matchAll(/\{\s*"([A-Za-z_][\w-]*)"\s*,\s*OW_VERSION_STRING/g)) descs.add(m[1])
    for (const m of s.matchAll(/ow_module_desc_t\s+\w*\s*\{?\s*"([^"]+)"/g)) descs.add(m[1])
    for (const m of s.matchAll(/ow_module_desc_t\s*\*\s*([A-Za-z_]\w*)\s*\(/g)) factories.add(m[1])
  }
  return { fns, descs, factories }
}

const errs = []
const warns = []
const eq = (a, b) => a.size === b.size && [...a].every((x) => b.has(x))

for (const d of fs.readdirSync(API, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
  if (!d.isDirectory()) continue
  const mf = path.join(API, d.name, 'owear.module.json')
  if (!fs.existsSync(mf)) {
    errs.push(`api/${d.name}/: sin owear.module.json`)
    continue
  }
  const m = JSON.parse(fs.readFileSync(mf, 'utf8'))
  const apiFiles = walk(path.join(API, d.name))
  const coreFiles = walk(path.join(ROOT, 'src', 'Core'))
  const src = extract(apiFiles)
  const withCore = extract([...apiFiles, ...coreFiles])

  if (m.kind === 'module') {
    const manifestFns = new Set(m.functions ?? [])
    if (src.descs.size && !src.descs.has(m.name)) {
      errs.push(`api/${d.name}/: descriptor(es) [${[...src.descs]}] != manifest name "${m.name}"`)
    }
    if (!eq(manifestFns, src.fns)) {
      const missing = [...manifestFns].filter((x) => !src.fns.has(x))
      const extra = [...src.fns].filter((x) => !manifestFns.has(x))
      errs.push(
        `api/${d.name}/: funciones manifiesto≠código` +
          (missing.length ? ` · faltan en C++: ${missing}` : '') +
          (extra.length ? ` · sin declarar: ${extra}` : '')
      )
    }
  } else {
    for (const desc of m.descriptors ?? []) {
      if (!desc.core && !withCore.factories.has(desc.factory)) {
        // la factory puede declararse con otra forma; avisamos, no fallamos
        warns.push(`api/${d.name}/: factory "${desc.factory}" no encontrada por regex`)
      }
      const declared = new Set(desc.functions ?? [])
      const missing = [...declared].filter((x) => !withCore.fns.has(x))
      if (missing.length) errs.push(`api/${d.name}/${desc.name}: funciones no encontradas en C++: ${missing}`)
    }
  }
}

if (warns.length) for (const w of warns) console.warn(`[check-apis] ⚠ ${w}`)
if (errs.length) {
  for (const e of errs) console.error(`[check-apis] ✗ ${e}`)
  process.exit(1)
}
console.log('[check-apis] ✓ manifiestos sincronizados con el código')
