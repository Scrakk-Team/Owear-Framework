// Copyright 2026 Owear Contributors — SPDX-License-Identifier: Apache-2.0
window.BENCH = {
  name: 'electron',
  invoke: (x) => window.bench.invoke(x),
  on: (ev, cb) => window.bench.on(ev, cb),
  burst: (n) => window.bench.burst(n),
  big: (n) => window.bench.big(n),
  ready: () => window.bench.ready(),
  report: (json) => window.bench.report(json),
  readFile: async () => {
    const r = await window.bench.readfile()
    return (r && (r.byteLength || r.length)) || 0
  },
  done: () => window.bench.done(),
}
