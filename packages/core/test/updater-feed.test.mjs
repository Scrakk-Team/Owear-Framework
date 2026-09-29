// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Test de integración del auto-update: genera un artefacto nuevo + blockmap +
// manifiesto YAML firmado con `tools/owear-update.mjs`, lo sirve por HTTP con
// soporte de Range y comprueba que el SDK:
//   1. parsea el manifiesto YAML y verifica la firma Ed25519,
//   2. planifica el delta y descarga **solo los bloques cambiados**,
//   3. ensambla el artefacto nuevo y coincide sha256.

import { test } from 'node:test'
import assert from 'node:assert/strict'
import * as crypto from 'node:crypto'
import * as fs from 'node:fs'
import * as http from 'node:http'
import * as os from 'node:os'
import * as path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

import { parseYaml } from '../dist/updater/yaml.js'
import { assemble, blockHashes, planDelta } from '../dist/updater/delta.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const TOOL = path.resolve(__dirname, '../../cli/tools/owear-update.mjs')

function serveWithRange(root) {
  const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://localhost')
    const p = path.join(root, path.basename(url.pathname))
    if (!fs.existsSync(p) || !fs.statSync(p).isFile()) {
      res.writeHead(404).end('no')
      return
    }
    const data = fs.readFileSync(p)
    const range = req.headers.range
    if (range) {
      const m = /bytes=(\d+)-(\d+)/.exec(range)
      if (!m) {
        res.writeHead(416).end()
        return
      }
      const start = +m[1]
      const end = Math.min(+m[2], data.length - 1)
      res.writeHead(206, {
        'Content-Range': `bytes ${start}-${end}/${data.length}`,
        'Content-Length': String(end - start + 1),
      })
      res.end(data.subarray(start, end + 1))
      return
    }
    res.writeHead(200, { 'Content-Length': String(data.length) })
    res.end(data)
  })
  return server
}

test('feed: manifiesto YAML + firma + delta por HTTP Range', async (t) => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'owear-feed-'))
  const blockSize = 4096

  // artefacto "antiguo" (instalado) y "nuevo" (publicado); solo cambian 2 bloques
  const oldBuf = crypto.randomBytes(blockSize * 8)
  const newBuf = Buffer.from(oldBuf)
  newBuf.fill(0xab, blockSize * 2, blockSize * 3)
  newBuf.fill(0xcd, blockSize * 5, blockSize * 6)

  const artifact = path.join(dir, 'MiApp-1.4.0')
  fs.writeFileSync(artifact, newBuf)

  const server = serveWithRange(dir)
  await new Promise((r) => server.listen(0, '127.0.0.1', r))
  const port = server.address().port
  t.after(() => server.close())

  const key = path.join(dir, 'signing')
  let r = spawnSync('node', [TOOL, '--gen-key', key], { encoding: 'utf8' })
  assert.equal(r.status, 0, r.stderr)
  assert.ok(fs.existsSync(`${key}.pem`))
  assert.ok(fs.existsSync(`${key}.pub.b64`))

  r = spawnSync(
    'node',
    [
      TOOL,
      '--file', artifact,
      '--version', '1.4.0',
      '--channel', 'latest',
      '--url', `http://127.0.0.1:${port}`,
      '--key', `${key}.pem`,
      '--block-size', String(blockSize),
      '--notes', 'Arreglos varios',
    ],
    { encoding: 'utf8' },
  )
  assert.equal(r.status, 0, r.stderr)

  const bm = JSON.parse(fs.readFileSync(path.join(dir, 'MiApp-1.4.0.blockmap'), 'utf8'))
  assert.equal(bm.blockSize, blockSize)
  assert.equal(bm.size, newBuf.length)
  assert.equal(bm.blocks.length, 8)

  // 1. manifiesto + firma
  const manifestText = fs.readFileSync(path.join(dir, 'latest.yml'), 'utf8')
  const manifest = parseYaml(manifestText)
  assert.equal(manifest.version, '1.4.0')
  assert.equal(manifest.sha256, crypto.createHash('sha256').update(newBuf).digest('hex'))
  assert.match(manifest.notes, /Arreglos varios/)

  const publicKey = crypto.createPublicKey({
    key: Buffer.from(fs.readFileSync(`${key}.pub.b64`, 'utf8'), 'base64'),
    format: 'der',
    type: 'spki',
  })
  const ok = crypto.verify(
    null,
    Buffer.from(`${manifest.version}:${manifest.sha256}`),
    publicKey,
    Buffer.from(manifest.signature, 'base64'),
  )
  assert.equal(ok, true, 'firma del manifiesto debe verificar')

  // 2. delta: solo 2 de 8 bloques cambian
  const plan = planDelta(blockHashes(oldBuf, blockSize), bm.blocks, blockSize, bm.size)
  assert.equal(plan.changed, 2)
  assert.equal(plan.reused, 6)
  assert.equal(plan.ranges.length, 2)
  assert.equal(plan.downloadBytes, blockSize * 2)

  // 3. descarga por rangos + ensambla
  const artifactUrl = `http://127.0.0.1:${port}/MiApp-1.4.0`
  let downloaded = 0
  const out = await assemble(oldBuf, bm.blocks, plan, bm.size, async (range) => {
    const res = await fetch(artifactUrl, { headers: { Range: `bytes=${range.start}-${range.end}` } })
    assert.equal(res.status, 206)
    const buf = Buffer.from(await res.arrayBuffer())
    downloaded += buf.length
    return buf
  })

  assert.equal(downloaded, blockSize * 2, 'solo se descargan los bloques cambiados')
  assert.equal(crypto.createHash('sha256').update(out).digest('hex'), manifest.sha256)
  assert.equal(Buffer.compare(out, newBuf), 0)

  fs.rmSync(dir, { recursive: true, force: true })
})
