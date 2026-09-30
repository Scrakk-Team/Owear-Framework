// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// benchmarks/neutralino/ext/bench.js — extensión de Neutralino para el bench.
// Implementa el backend nativo (eco, burst de eventos, payload, fichero) y se
// comunica por WebSocket con el server de Neutralino.
const fs = require('node:fs')
const { randomUUID } = require('node:crypto')

const OUT = '/tmp/opencode/bench/out/neutralino.json'
const BLOB = '/tmp/opencode/bench/blob.bin'
const LOG = '/tmp/opencode/bench/out/neutralino-ext.log'
const log = (m) => {
  try {
    fs.appendFileSync(LOG, m + '\n')
  } catch {}
}
log('start')

// Neutralino pasa la info de conexión por stdin.
const cfg = JSON.parse(fs.readFileSync(0, 'utf8'))
log('cfg ' + JSON.stringify(cfg))
const { nlPort, nlToken, nlConnectToken, nlExtensionId } = cfg

const ws = new WebSocket(
  `ws://localhost:${nlPort}?extensionId=${nlExtensionId}&connectToken=${nlConnectToken}`,
)

function native(method, data) {
  ws.send(JSON.stringify({ id: randomUUID(), method, accessToken: nlToken, data }))
}

function broadcast(event, data) {
  native('app.broadcast', { event, data })
}

function reply(id, value, error) {
  broadcast('bench-reply', { id, value: value === undefined ? null : value, error: error || null })
}

ws.onopen = () => log('ws open')
ws.onclose = () => {
  log('ws close')
  process.exit(0)
}
ws.onerror = (e) => {
  log('ws error ' + (e && e.message))
  process.exit(1)
}

ws.onmessage = (e) => {
  log('ws msg ' + String(e.data).slice(0, 160))
  let msg
  try {
    msg = JSON.parse(e.data)
  } catch {
    return
  }
  const { event, data } = msg
  if (event !== 'bench') return
  const { id, fn, args = [] } = data
  try {
    switch (fn) {
      case 'echo':
        reply(id, args[0])
        break
      case 'debug':
        log('adapter: ' + String(args[0]))
        reply(id, null)
        break
      case 'burst': {
        const n = Number(args[0]) || 5000
        for (let i = 0; i < n; i++) broadcast('bench-tick', i)
        reply(id, null)
        break
      }
      case 'big':
        reply(id, 'y'.repeat(Number(args[0]) || 5242880))
        break
      case 'readFile': {
        const buf = fs.readFileSync(BLOB)
        reply(id, buf.toString('latin1').length)
        break
      }
      case 'ready': {
        fs.writeFileSync(String(args[0]), '1')
        process.stdout.write('BENCH_READY\n')
        reply(id, null)
        break
      }
      case 'report':
        fs.writeFileSync(OUT, String(args[0]))
        reply(id, null)
        break
      case 'done':
        fs.writeFileSync(OUT + '.done', '1')
        reply(id, null)
        setTimeout(() => process.exit(0), 150)
        break
      default:
        reply(id, null, 'fn desconocida: ' + fn)
    }
  } catch (err) {
    reply(id, null, String(err))
  }
}
