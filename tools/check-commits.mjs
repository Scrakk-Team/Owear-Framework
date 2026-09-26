// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// tools/check-commits.mjs — valida Conventional Commits en un rango de git.
//
//   node tools/check-commits.mjs                (HEAD~1..HEAD)
//   node tools/check-commits.mjs A..B
//
// Tipos permitidos: build chore ci docs feat fix perf refactor revert style test
// Formato: <tipo>(<scope>)?(!)?: <asunto>   · asunto ≤ 100 caracteres
//

import { execFileSync } from 'node:child_process'

const range = process.argv[2] || 'HEAD~1..HEAD'
const ALLOWED = 'build|chore|ci|docs|feat|fix|perf|refactor|revert|style|test'
const RE = new RegExp(`^(${ALLOWED})(\\([a-z0-9._/-]+\\))?!?: .+`)

let subjects = []
try {
  subjects = execFileSync('git', ['log', '--format=%s', range], { encoding: 'utf8' })
    .split('\n')
    .filter(Boolean)
} catch {
  console.error(`[commits] no se pudo leer el rango ${range}`)
  process.exit(1)
}

const bad = []
for (const s of subjects) {
  if (/^Merge /.test(s)) continue // merges de git
  if (/^(fixup|squash)!/.test(s)) continue
  if (!RE.test(s)) bad.push(`${s}  (no sigue <tipo>(scope): asunto)`)
  else if (s.length > 100) bad.push(`${s}  (asunto de ${s.length} > 100)`)
}

if (bad.length) {
  console.error(`[commits] ✗ ${bad.length} commit(s) inválidos en ${range}:`)
  for (const b of bad) console.error('  ' + b)
  console.error(`[commits] tipos: ${ALLOWED.replaceAll('|', ', ')}`)
  process.exit(1)
}
console.log(`[commits] ✓ ${subjects.length} commit(s) válidos`)
