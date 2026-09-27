// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// packages/core/src/node/worker.ts — worker Node con canal de mensajes.
//
// Reemplazo Owear de `utilityProcess.fork` de Electron. El proceso principal
// de Owear YA es Node real, así que un worker es un hijo con canal IPC
// (`child_process.fork`). Los workers portados desde Electron que usan
// `process.parentPort` funcionan sin cambios: worker-runner.ts lo emula.
//
//   const w = app.forkWorker('tree-sitter-worker.js')
//   w.on('message', (m) => …)
//   w.postMessage({ … })

import { fork, type ChildProcess } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { dirname, isAbsolute, join, resolve } from 'node:path'

const runnerPath = join(dirname(fileURLToPath(import.meta.url)), 'worker-runner.js')

export interface WorkerHandle {
  /** Envía un mensaje al worker (structured clone de Node). */
  postMessage(message: unknown): void
  /** Escucha los mensajes del worker o su salida. */
  on(event: 'message', listener: (message: unknown) => void): void
  on(event: 'exit', listener: (code: number) => void): void
  /** Termina el worker. */
  kill(signal?: NodeJS.Signals): void
  /** PID del worker (undefined hasta que el SO lo asigna). */
  readonly pid: number | undefined
}

export interface ForkWorkerOptions {
  args?: string[]
  cwd?: string
  env?: NodeJS.ProcessEnv
  /** execArgv para el hijo (por defecto, el del proceso actual). */
  execArgv?: string[]
}

/**
 * Resuelve el entry de un worker. Si es relativo sin `./`, se busca en
 * `OW_APP_WORKERS` (lo define `ow dev`/`ow build`); si no, contra el cwd.
 * Las rutas absolutas y las que empiezan por `.` se respetan tal cual.
 */
export function resolveWorkerEntry(entry: string): string {
  if (isAbsolute(entry)) return entry
  if (entry.startsWith('.')) return resolve(entry)
  const dir = process.env.OW_APP_WORKERS
  return dir ? join(dir, entry) : resolve(entry)
}

/**
 * Lanza un worker Node con canal de mensajes. Devuelve un handle síncrono.
 * Los errores de arranque se reportan por el evento `exit` del worker.
 */
export function forkWorker(entry: string, options: ForkWorkerOptions = {}): WorkerHandle {
  const resolved = resolveWorkerEntry(entry)
  const child: ChildProcess = fork(runnerPath, [resolved, ...(options.args ?? [])], {
    cwd: options.cwd,
    env: options.env,
    execArgv: options.execArgv ?? process.execArgv,
    stdio: ['ignore', 'inherit', 'inherit', 'ipc'],
  })

  const handle = {
    postMessage(message: unknown): void {
      child.send(message as never)
    },
    on(event: 'message' | 'exit', listener: (arg: never) => void): void {
      if (event === 'message') child.on('message', (m: unknown) => listener(m as never))
      else child.on('exit', (code) => listener((code ?? 0) as never))
    },
    kill(signal?: NodeJS.Signals): void {
      child.kill(signal)
    },
    get pid(): number | undefined {
      return child.pid
    },
  }

  return handle as unknown as WorkerHandle
}
