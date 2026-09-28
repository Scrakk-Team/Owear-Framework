#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// ─────────────────────────────────────────────────────────────────────────────
// tools/license-header.mjs — añade la cabecera SPDX Apache-2.0 a los ficheros
// de código tracked que aún no la tienen.
//
//   node tools/license-header.mjs           # aplica
//   node tools/license-header.mjs --check   # falla si falta alguna (CI)
//
// Idempotente (salta los que ya tienen SPDX). Excluye terceros (deps/, minjson),
// generados, binarios, JSON (no admite comentarios), lockfiles y dotfiles.
// ─────────────────────────────────────────────────────────────────────────────

import { execFileSync } from 'node:child_process'
import * as fs from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const CHECK = process.argv.includes('--check')
const YEAR = 2026

// extensión → estilo de comentario
const STYLES = {
  '//': ['cpp', 'cc', 'cxx', 'hpp', 'hh', 'h', 'inl', 'm', 'mm', 'ts', 'tsx', 'js', 'mjs', 'cjs'],
  '#': ['py', 'sh', 'bash', 'yml', 'yaml', 'cmake', 'txt'],
  '/*': ['css'],
  '<!--': ['html', 'htm', 'md', 'markdown'],
}
const EXT_STYLE = {}
for (const [style, exts] of Object.entries(STYLES)) for (const e of exts) EXT_STYLE[e] = style

// binarios / no-comentables por extensión
const SKIP_EXT = new Set([
  'json', 'lib', 'bin', 'woff2', 'woff', 'ttf', 'otf', 'ico', 'png', 'jpg', 'jpeg',
  'gif', 'webp', 'svg', 'lock', 'pdf', 'exe', 'dll', 'so', 'dylib', 'owm', 'a', 'o',
])

function skip(file) {
  const base = path.basename(file)
  if (base.startsWith('.')) return true // dotfiles (.gitignore, …)
  if (base === 'LICENSE' || base === 'pnpm-lock.yaml' || base === 'package-lock.json') return true
  if (file.startsWith('deps/')) return true // WebView2 SDK vendido (Microsoft)
  if (file === 'include/ow/detail/minjson.hpp') return true // tercero
  if (file.includes('.generated.')) return true
  if (file === 'src/api/generated.cmake' || file === 'src/Core/builtins.generated.cmake') return true
  const ext = base.includes('.') ? base.slice(base.lastIndexOf('.') + 1).toLowerCase() : ''
  if (SKIP_EXT.has(ext)) return true
  return false
}

function headerFor(style) {
  const c1 = 'Copyright ' + YEAR + ' Owear Contributors'
  const c2 = 'SPDX-License-Identifier: Apache-2.0'
  switch (style) {
    case '//':
      return `// ${c1}\n// ${c2}\n//\n`
    case '#':
      return `# ${c1}\n# ${c2}\n#\n`
    case '/*':
      return `/* ${c1}\n * ${c2}\n */\n`
    case '<!--':
      return `<!-- ${c1}\n     ${c2} -->\n`
  }
  return ''
}

function applyHeader(file, style) {
  let src = fs.readFileSync(file, 'utf8')
  // ya tiene SPDX en las primeras líneas → nada
  if (/SPDX-License-Identifier/.test(src.split('\n', 25).join('\n'))) return false

  const header = headerFor(style)
  let insertAt = 0
  const firstLineEnd = src.indexOf('\n')
  const firstLine = firstLineEnd === -1 ? src : src.slice(0, firstLineEnd)
  if (firstLine.startsWith('#!')) insertAt = firstLineEnd + 1 // tras shebang
  else {
    const m = src.match(/^\s*<!doctype[^>]*>\s*\n/i)
    if (m) insertAt = m[0].length // tras <!doctype html>
  }

  // para html/md el bloque cierra sin línea vacía de comentario; añadimos un \n
  const spacer = style === '<!--' || style === '/*' ? '\n' : ''
  src = src.slice(0, insertAt) + header + spacer + src.slice(insertAt)
  fs.writeFileSync(file, src)
  return true
}

const files = execFileSync('git', ['ls-files'], { cwd: ROOT, encoding: 'utf8' })
  .split('\n')
  .filter(Boolean)

let changed = 0
let pending = 0
for (const rel of files) {
  if (skip(rel)) continue
  const ext = rel.includes('.') ? rel.slice(rel.lastIndexOf('.') + 1).toLowerCase() : ''
  const style = EXT_STYLE[ext]
  if (!style) continue
  const abs = path.join(ROOT, rel)
  if (!fs.existsSync(abs)) continue
  const src = fs.readFileSync(abs, 'utf8')
  const has = /SPDX-License-Identifier/.test(src.split('\n', 25).join('\n'))
  if (has) continue
  pending++
  if (CHECK) {
    console.error(`[license] falta SPDX: ${rel}`)
    continue
  }
  if (applyHeader(abs, style)) changed++
}

if (CHECK) {
  if (pending) {
    console.error(`[license] ✗ ${pending} fichero(s) sin cabecera — corre: node tools/license-header.mjs`)
    process.exit(1)
  }
  console.log('[license] ✓ todos los ficheros con cabecera')
} else {
  console.log(`[license] ${changed} fichero(s) actualizados`)
}
