// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// packages/core/src/node/worker-runner.ts — arranque de un worker Node.
//
// Lo lanza `app.forkWorker()` (ver worker.ts). Instala el shim
// `process.parentPort` (estilo `utilityProcess` de Electron) para que los
// workers portados desde Electron funcionen SIN cambios, y carga el entry real.
//
// Invocación:  node [execArgv] worker-runner.js <entry> [...args]

import { pathToFileURL } from 'node:url'

type ParentPortListener = (event: { data: unknown }) => void

interface ParentPort {
  postMessage(message: unknown): void
  on(event: string, listener: ParentPortListener): ParentPort
  start(): void
}

const parentPort: ParentPort = {
  postMessage(message: unknown): void {
    // El canal IPC lo abre `child_process.fork`; si no existe, es un noop.
    process.send?.(message as never)
  },
  on(event: string, listener: ParentPortListener): ParentPort {
    if (event === 'message') {
      process.on('message', (message) => listener({ data: message }))
    }
    return parentPort
  },
  start(): void {
    /* noop: el canal IPC ya está activo */
  },
}

// Shim aditivo: sólo define `parentPort` si no existe (nunca pisa uno real).
;(process as unknown as { parentPort?: unknown }).parentPort ??= parentPort

const entry = process.argv[2]
if (!entry) {
  console.error('[ow] forkWorker: falta el entry del worker')
  process.exit(1)
}

// El worker debe ver SU argv, no el del runner.
process.argv = [process.execPath, entry, ...process.argv.slice(3)]

await import(pathToFileURL(entry).href)
