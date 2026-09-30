---
title: Workers
description: app.forkWorker launches a Node child process with an IPC channel — the
order: 22
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Workers

`app.forkWorker` launches a Node child process with an IPC channel — the
replacement for Electron's `utilityProcess.fork`.

```ts
import { app, forkWorker, resolveWorkerEntry } from '@owear/core'

const w = app.forkWorker('tree-sitter-worker.js')
w.on('message', (m) => console.log(m))
w.postMessage({ op: 'tokenize', text })
w.on('exit', (code) => console.log('exited', code))
w.kill()
```

## Entry resolution

```ts
resolveWorkerEntry(entry: string): string
```

- absolute paths are used as-is;
- entries starting with `.` resolve against the current working directory;
- other relative entries resolve against `OW_APP_WORKERS` (set by
  `ow dev`/`ow build`), or the cwd if it is not defined.

So `app.forkWorker('tree-sitter-worker.js')` finds the compiled worker from
`app/workers/tree-sitter-worker.ts`.

## API

```ts
forkWorker(entry: string, options?: ForkWorkerOptions): WorkerHandle

interface ForkWorkerOptions {
  args?: string[]
  cwd?: string
  env?: NodeJS.ProcessEnv
  execArgv?: string[]        // defaults to the current process's execArgv
}

interface WorkerHandle {
  postMessage(message: unknown): void
  on(event: 'message', listener: (message: unknown) => void): void
  on(event: 'exit', listener: (code: number) => void): void
  kill(signal?: NodeJS.Signals): void
  readonly pid: number | undefined
}
```

The returned handle is synchronous; startup errors are reported through the
worker's `exit` event.

## Compilation

The CLI compiles `app/workers/**/*.{ts,mts,js,mjs}` to `dist/workers/` (or
`.owear/workers/` in dev) and exposes the directory through `OW_APP_WORKERS`.
Workers run on the same real Node as the main process, so they can load N-API
addons (see [Native addons](../../guides/native-addons.md)).

## Electron compatibility

Workers ported from Electron that use `process.parentPort` work unchanged: the
worker runner shims it.
