// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// benchmarks/owear/assets/adapter.js — adaptador del bridge de Owear.
const OUT =
  new URLSearchParams(location.search).get('out') || '/tmp/opencode/bench/out/owear.json'
const BLOB = '/tmp/opencode/bench/blob.bin'

window.BENCH = {
  name: 'owear',
  invoke: (x) => window.ow.invoke('bench', 'echo', x),
  on: (ev, cb) => window.ow.on(ev, cb),
  burst: (n) => window.ow.invoke('bench', 'burst', n),
  big: (n) => window.ow.invoke('bench', 'big', n),
  ready: async () => {
    await window.ow.invoke('bench', 'mark', OUT.replace(/\.json$/, '.ready'))
    return window.ow.invoke('bench', 'ready')
  },
  report: (json) => window.ow.invoke('bench', 'report', OUT, json),
  readFile: async () => {
    const r = await window.ow.invoke('fs', 'readFile', BLOB)
    if (r && r.__ow_shm) {
      const buf = await window.ow.readShared(r.__ow_shm)
      return buf.byteLength
    }
    return (r && (r.byteLength || r.length)) || 0
  },
  done: async () => {
    await window.ow.invoke('bench', 'mark', OUT + '.done').catch(() => {})
    await window.ow
      .invoke('node', 'call', { fn: 'bench.done', args: [] })
      .catch(() => {})
  },
}
