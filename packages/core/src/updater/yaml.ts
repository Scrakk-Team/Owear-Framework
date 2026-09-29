// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/updater/yaml.ts — parser YAML mínimo (subconjunto) para los
// manifiestos de auto-update. Soporta:
//   - mapas (`key: value`), anidados por indentación
//   - listas (`- item`) de escalares o mapas
//   - escalares (texto, comillas simples/dobles, números, true/false/null)
//   - bloque literal (`key: |` y `key: >`) con líneas indentadas
// No soporta: anchors/aliases, flow style, multi-documento, tags. Suficiente
// para `latest.yml` / `<channel>.yml` (estilo electron-builder).

export type YamlValue = string | number | boolean | null | YamlMap | YamlValue[]
export interface YamlMap {
  [k: string]: YamlValue
}

interface Line {
  indent: number
  text: string
}

function stripComment(s: string): string {
  // Solo elimina comentarios fuera de comillas.
  let inS = false
  let inD = false
  for (let i = 0; i < s.length; i++) {
    const c = s[i]
    if (c === "'" && !inD) inS = !inS
    else if (c === '"' && !inS) inD = !inD
    else if (c === '#' && !inS && !inD && (i === 0 || s[i - 1] === ' ')) return s.slice(0, i)
  }
  return s
}

function tokenize(text: string): Line[] {
  const out: Line[] = []
  for (const raw of text.replace(/\r\n?/g, '\n').split('\n')) {
    const noComment = stripComment(raw)
    if (noComment.trim() === '' || /^---\s*$/.test(noComment.trim()) || /^\.\.\.\s*$/.test(noComment.trim()))
      continue
    const indent = noComment.match(/^ */)![0].length
    out.push({ indent, text: noComment.slice(indent) })
  }
  return out
}

function parseScalar(s: string): YamlValue {
  const t = s.trim()
  if (t === '' || t === '~' || t === 'null') return null
  if (t === 'true') return true
  if (t === 'false') return false
  if (/^'.*'$/.test(t)) return t.slice(1, -1).replace(/''/g, "'")
  if (/^".*"$/.test(t)) {
    try {
      return JSON.parse(t) as string
    } catch {
      return t.slice(1, -1)
    }
  }
  if (/^-?\d+$/.test(t)) return parseInt(t, 10)
  if (/^-?\d*\.\d+$/.test(t)) return parseFloat(t)
  return t
}

/** Divide "key: value" por el primer ':' seguido de espacio o fin. */
function splitKey(text: string): { key: string; rest: string } | null {
  let inS = false
  let inD = false
  for (let i = 0; i < text.length; i++) {
    const c = text[i]
    if (c === "'" && !inD) inS = !inS
    else if (c === '"' && !inS) inD = !inD
    else if (c === ':' && !inS && !inD && (i + 1 === text.length || text[i + 1] === ' '))
      return { key: text.slice(0, i).trim(), rest: text.slice(i + 1).trim() }
  }
  return null
}

function parseBlock(lines: Line[], start: number, indent: number): { value: YamlValue; next: number } {
  if (start >= lines.length) return { value: null, next: start }
  const first = lines[start]

  // Lista
  if (first.text.startsWith('- ') || first.text === '-') {
    const arr: YamlValue[] = []
    let i = start
    while (i < lines.length && lines[i].indent === indent && (lines[i].text.startsWith('- ') || lines[i].text === '-')) {
      const item = lines[i].text === '-' ? '' : lines[i].text.slice(2)
      if (item === '') {
        const sub = parseBlock(lines, i + 1, lines[i + 1]?.indent ?? indent + 2)
        arr.push(sub.value)
        i = sub.next
      } else {
        const kv = splitKey(item)
        if (kv) {
          // mapa en línea (primer campo) + campos siguientes más indentados
          const obj: YamlMap = {}
          obj[kv.key] = kv.rest === '' ? null : parseScalar(kv.rest)
          const sub = parseBlock(lines, i + 1, lines[i + 1]?.indent ?? indent + 2)
          if (sub.value && typeof sub.value === 'object' && !Array.isArray(sub.value)) {
            Object.assign(obj, sub.value as YamlMap)
            i = sub.next
          } else {
            i++
          }
          arr.push(obj)
        } else {
          arr.push(parseScalar(item))
          i++
        }
      }
    }
    return { value: arr, next: i }
  }

  // Mapa
  const obj: YamlMap = {}
  let i = start
  while (i < lines.length && lines[i].indent === indent) {
    const kv = splitKey(lines[i].text)
    if (!kv) break
    if (kv.rest === '|' || kv.rest === '>') {
      const blockIndent = lines[i + 1]?.indent ?? indent + 2
      const parts: string[] = []
      i++
      while (i < lines.length && lines[i].indent >= blockIndent) {
        parts.push(lines[i].text)
        i++
      }
      obj[kv.key] = kv.rest === '>' ? parts.join(' ') : parts.join('\n')
      continue
    }
    if (kv.rest === '') {
      // valor anidado (mapa/lista) o null
      const nextLine = lines[i + 1]
      if (nextLine && nextLine.indent > indent) {
        const sub = parseBlock(lines, i + 1, nextLine.indent)
        obj[kv.key] = sub.value
        i = sub.next
      } else {
        obj[kv.key] = null
        i++
      }
    } else {
      obj[kv.key] = parseScalar(kv.rest)
      i++
    }
  }
  return { value: obj, next: i }
}

/** Parsea un manifiesto YAML a un objeto JS. */
export function parseYaml(text: string): YamlValue {
  const lines = tokenize(text)
  if (!lines.length) return null
  return parseBlock(lines, 0, lines[0].indent).value
}
