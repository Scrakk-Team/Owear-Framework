// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// benchmarks/neutralino/resources/adapter.js — adaptador del bench para
// Neutralino. El "invoke" va renderer → server → EXTENSIÓN → server → renderer
// (es el modelo de Neutralino: no hay IPC nativo genérico sin extensión).
const NL = window.Neutralino
const EXT = 'js.owear.bench'
const READY = '/tmp/opencode/bench/out/neutralino.ready'

let seq = 0
const pending = new Map()

function onReply(e) {
  const { id, value, error } = (e && e.detail) || {}
  const p = pending.get(id)
  if (!p) return
  pending.delete(id)
  if (error) p.reject(new Error(error))
  else p.resolve(value)
}

const started = Promise.resolve()
  .then(() => NL.init())
  .then(() => {
    NL.events.on('bench-reply', onReply)
  })

function call(fn, args) {
  return started.then(
    () =>
      new Promise((resolve, reject) => {
        const id = ++seq
        pending.set(id, { resolve, reject })
        NL.extensions.dispatch(EXT, 'bench', { id, fn, args }).catch(reject)
      }),
  )
}

window.BENCH = {
  name: 'neutralino',
  invoke: (x) => call('echo', [x]),
  on: (ev, cb) => {
    void started.then(() => NL.events.on(ev, (e) => cb(e && e.detail)))
  },
  burst: (n) => call('burst', [n]),
  big: (n) => call('big', [n]),
  ready: async () => {
    await call('ready', [READY])
    return null
  },
  report: (json) => call('report', [json]),
  readFile: async () => {
    const len = await call('readFile', [])
    return (len && Number(len)) || 0
  },
  done: () => call('done', []),
}
