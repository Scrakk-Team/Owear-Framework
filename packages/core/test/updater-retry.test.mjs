// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests de robustez de red del autoUpdater: reintentos con backoff, reanudación
// de descargas (Range) y timeout por petición. Sin kernel (estado nativo vacío).

import { test } from 'node:test'
import assert from 'node:assert/strict'
import * as crypto from 'node:crypto'
import * as fs from 'node:fs'
import * as http from 'node:http'

import { autoUpdater } from '../dist/updater.js'

// sin control socket: invokeNative rechaza al instante (no cuelga)
process.env.OW_CONTROL_SOCKET = '/tmp/owear-no-existe.sock'

function start(handler) {
  return new Promise((resolve) => {
    const server = http.createServer(handler)
    server.listen(0, '127.0.0.1', () => resolve({ server, port: server.address().port }))
  })
}

test('reintentos: el manifiesto falla 2 veces (500) y luego responde', async (t) => {
  let hits = 0
  const { server, port } = await start((req, res) => {
    if (req.url.endsWith('.yml')) {
      hits++
      if (hits < 3) {
        res.writeHead(500).end('boom')
        return
      }
      res.writeHead(200, { 'Content-Type': 'text/yaml' }).end(
        `version: 9.9.9\npath: app.bin\nsha256: ${'0'.repeat(64)}\nsize: 1\n`,
      )
      return
    }
    res.writeHead(404).end()
  })
  t.after(() => server.close())

  autoUpdater.setFeedURL({ provider: 'generic', url: `http://127.0.0.1:${port}` })
  autoUpdater.autoDownload = false
  autoUpdater.maxRetries = 3
  autoUpdater.retryDelay = 10

  const res = await autoUpdater.checkForUpdates()
  assert.ok(res, 'debe resolverse tras los reintentos')
  assert.equal(res.updateInfo.version, '9.9.9')
  assert.equal(hits, 3, 'el servidor debe haber recibido 3 intentos')
})

test('reanudación: la conexión cae a mitad y se reanuda con Range', async (t) => {
  const payload = crypto.randomBytes(64 * 1024)
  const sha = crypto.createHash('sha256').update(payload).digest('hex')
  let sawRange = false
  let fullRequests = 0

  const { server, port } = await start((req, res) => {
    if (req.url.endsWith('.yml')) {
      res.writeHead(200).end(`version: 9.9.9\npath: app.bin\nsha256: ${sha}\nsize: ${payload.length}\n`)
      return
    }
    if (req.url.endsWith('app.bin')) {
      const range = req.headers.range
      if (range) {
        sawRange = true
        const m = /bytes=(\d+)-/.exec(range)
        const start = +m[1]
        const rest = payload.subarray(start)
        res.writeHead(206, { 'Content-Range': `bytes ${start}-${payload.length - 1}/${payload.length}`, 'Content-Length': String(rest.length) })
        res.end(rest)
        return
      }
      // primera petición completa: envía la mitad y corta la conexión
      fullRequests++
      res.writeHead(200, { 'Content-Length': String(payload.length) })
      res.write(payload.subarray(0, payload.length / 2))
      setTimeout(() => res.destroy(), 20)
      return
    }
    res.writeHead(404).end()
  })
  t.after(() => server.close())

  autoUpdater.setFeedURL({ provider: 'generic', url: `http://127.0.0.1:${port}` })
  autoUpdater.autoDownload = false
  autoUpdater.maxRetries = 3
  autoUpdater.retryDelay = 10

  const check = await autoUpdater.checkForUpdates()
  assert.ok(check)
  const [file] = await autoUpdater.downloadUpdate()

  assert.equal(sawRange, true, 'debe reanudar con Range')
  assert.equal(fullRequests, 1, 'la descarga completa solo se pide una vez')
  const got = fs.readFileSync(file)
  assert.equal(got.length, payload.length)
  assert.equal(crypto.createHash('sha256').update(got).digest('hex'), sha)
})

test('timeout: una petición que no responde se aborta y se reintenta', async (t) => {
  let hits = 0
  const { server, port } = await start((req, res) => {
    hits++
    if (hits === 1) {
      // nunca responde dentro del timeout
      setTimeout(() => {
        try {
          res.writeHead(200).end('version: 9.9.9\npath: app.bin\nsha256: ' + '0'.repeat(64) + '\nsize: 1\n')
        } catch {
          /* conexión ya abortada */
        }
      }, 1500)
      return
    }
    res.writeHead(200).end(`version: 9.9.9\npath: app.bin\nsha256: ${'0'.repeat(64)}\nsize: 1\n`)
  })
  t.after(() => server.close())

  autoUpdater.setFeedURL({ provider: 'generic', url: `http://127.0.0.1:${port}` })
  autoUpdater.autoDownload = false
  autoUpdater.maxRetries = 3
  autoUpdater.retryDelay = 10
  autoUpdater.requestTimeout = 200

  const res = await autoUpdater.checkForUpdates()
  assert.ok(res, 'debe resolverse tras abortar y reintentar')
  assert.ok(hits >= 2, `debe haber reintentado (hits=${hits})`)
})

test('binarySig sin clave pública no aborta el check (best-effort)', async (t) => {
  const { server, port } = await start((req, res) => {
    res.writeHead(200).end(`version: 9.9.9\npath: app.bin\nsha256: ${'0'.repeat(64)}\nsize: 1\nbinarySig: c2ln\n`)
  })
  t.after(() => server.close())

  autoUpdater.setFeedURL({ provider: 'generic', url: `http://127.0.0.1:${port}` })
  autoUpdater.autoDownload = false
  const res = await autoUpdater.checkForUpdates()
  assert.ok(res, 'un feed firmado no debe romper apps sin publicKey')
  assert.equal(res.updateInfo.binarySig, 'c2ln')
})

test('blockmap relativo se resuelve contra el manifiesto', async (t) => {
  const { server, port } = await start((req, res) => {
    res.writeHead(200).end(
      `version: 9.9.9\npath: app.bin\nsha256: ${'0'.repeat(64)}\nsize: 1\nblockSize: 4096\nblockmap: app.bin.blockmap\n`,
    )
  })
  t.after(() => server.close())

  autoUpdater.setFeedURL({ provider: 'generic', url: `http://127.0.0.1:${port}` })
  autoUpdater.autoDownload = false
  const res = await autoUpdater.checkForUpdates()
  assert.ok(res)
  assert.equal(res.updateInfo.blockmap, `http://127.0.0.1:${port}/app.bin.blockmap`)
})

test('sha256 incorrecto en el manifiesto → la descarga falla', async (t) => {
  const payload = crypto.randomBytes(4096)
  const { server, port } = await start((req, res) => {
    if (req.url.endsWith('.yml')) {
      res.writeHead(200).end(`version: 9.9.9\npath: app.bin\nsha256: ${'f'.repeat(64)}\nsize: ${payload.length}\n`)
      return
    }
    res.writeHead(200, { 'Content-Length': String(payload.length) }).end(payload)
  })
  t.after(() => server.close())

  autoUpdater.setFeedURL({ provider: 'generic', url: `http://127.0.0.1:${port}` })
  autoUpdater.autoDownload = false
  autoUpdater.retryDelay = 10
  await autoUpdater.checkForUpdates()
  await assert.rejects(() => autoUpdater.downloadUpdate(), /sha256/)
})
