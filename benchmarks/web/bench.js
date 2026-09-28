// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// benchmarks/web/bench.js — lógica de benchmark COMPARTIDA por Owear, Electron
// y Tauri. Cada app define `window.BENCH` (adaptador).
//
// Fases: idle (RAM), smoke, latencia IPC, IPC concurrente, payload (texto),
// binario (lectura de archivo 5MB: SHM/copia), eventos nativo→renderer, JS.

(function () {
  const B = window.BENCH
  const results = { framework: B.name }
  const IDLE_MS = 3000

  const now = () => performance.now()
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

  function withTimeout(p, ms, tag) {
    return Promise.race([
      p,
      new Promise((_, rej) => setTimeout(() => rej(new Error('timeout ' + tag)), ms)),
    ])
  }

  function summarize(arr) {
    const a = arr.slice().sort((x, y) => x - y)
    const q = (p) => a[Math.min(a.length - 1, Math.floor(p * a.length))]
    return { min: a[0], median: q(0.5), p95: q(0.95), p99: q(0.99), max: a[a.length - 1] }
  }

  const flush = () => B.report(JSON.stringify(results)).catch(() => {})

  async function idle() {
    await sleep(IDLE_MS) // ventana de RAM en reposo para el runner
    return { idleMs: IDLE_MS }
  }

  async function measureLatency(n) {
    for (let i = 0; i < 100; i++) await withTimeout(B.invoke(i), 5000, 'warmup')
    const times = []
    const t0 = now()
    for (let i = 0; i < n; i++) {
      const a = now()
      await withTimeout(B.invoke(i), 5000, 'invoke')
      times.push(now() - a)
    }
    const totalMs = now() - t0
    return { n, totalMs, ...summarize(times), opsPerSec: (n / totalMs) * 1000 }
  }

  async function measureConcurrent(n, width) {
    const times = []
    for (let i = 0; i < n; i += width) {
      const batch = []
      const a = now()
      for (let j = 0; j < width; j++) batch.push(B.invoke(i + j))
      await Promise.all(batch)
      times.push(now() - a)
    }
    const totalMs = times.reduce((x, y) => x + y, 0)
    return { n, width, totalMs, opsPerSec: (n / totalMs) * 1000 }
  }

  async function measurePayload(size, iterations) {
    const payload = 'x'.repeat(size)
    const times = []
    for (let i = 0; i < iterations; i++) {
      const a = now()
      const back = await withTimeout(B.invoke(payload), 30000, 'payload ' + size)
      const dt = now() - a
      if (typeof back !== 'string' || back.length !== size) {
        return { size, error: 'echo mismatch ' + (typeof back) }
      }
      times.push(dt)
    }
    return { size, iterations, medianMs: summarize(times).median }
  }

  async function measureBinary(iterations) {
    const times = []
    let bytes = 0
    for (let i = 0; i < iterations; i++) {
      const a = now()
      bytes = await withTimeout(B.readFile(), 30000, 'readFile')
      times.push(now() - a)
    }
    const median = summarize(times).median
    return { bytes, iterations, medianMs: median, mbPerSec: bytes / 1024 / 1024 / (median / 1000) }
  }

  async function measureEvents(n) {
    return await new Promise((resolve, reject) => {
      let got = 0
      let t0 = 0
      const timer = setTimeout(() => reject(new Error('timeout events ' + got + '/' + n)), 40000)
      Promise.resolve(
        B.on('bench-tick', () => {
          got++
          if (got === n) {
            clearTimeout(timer)
            const ms = now() - t0
            resolve({ n, ms, eventsPerSec: (n / ms) * 1000 })
          }
        }),
      )
        .then(() => {
          t0 = now()
          return B.burst(n)
        })
        .catch(reject)
    })
  }

  function measureJs() {
    const t0 = now()
    let s = 0
    for (let i = 0; i < 20000000; i++) s += i
    const loopMs = now() - t0
    const t1 = now()
    const arr = new Array(2000000)
    for (let i = 0; i < arr.length; i++) arr[i] = (i * 2654435761) % 2147483647
    arr.sort((a, b) => a - b)
    const sortMs = now() - t1
    return { loopMs, sortMs, checksum: s + arr[0] }
  }

  async function runBench() {
    try {
      await B.ready()
      results.ua = navigator.userAgent
      results.idle = await idle()
      await flush()

      results.latency = await measureLatency(2000)
      await flush()

      results.concurrent = await measureConcurrent(2000, 50)
      await flush()

      results.payload = []
      for (const size of [1024, 65536, 1048576, 5242880]) {
        results.payload.push(await measurePayload(size, size > 1048576 ? 3 : 10))
        await flush()
      }

      results.binary = await measureBinary(5)
      await flush()

      results.events = await measureEvents(5000)
      await flush()

      results.js = measureJs()
    } catch (e) {
      results.error = String(e)
    } finally {
      await flush()
      await B.done()
    }
  }

  window.runBench = runBench
  window.addEventListener('DOMContentLoaded', () => setTimeout(runBench, 200))
})()
