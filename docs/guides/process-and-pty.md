---
title: Processes and PTY
description: The process module spawns child processes with piped stdio and provides a real
order: 17
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Processes and PTY

The `process` module spawns child processes with piped stdio and provides a real
PTY manager for interactive terminals. It is the native building block for
embedded terminals, build runners, and language servers.

> If you need plain Node child processes, the main process already runs on Node:
> use `child_process` there. Use this module when you want the kernel to own the
> process and stream its output to the UI.

## Spawning with pipes

```ts
const { procId, pid } = await ow.invoke<{ procId: number; pid: number }>(
  'process', 'spawn',
  '/bin/bash',                 // command
  ['-lc', 'echo hi'],          // args
  '/home/me',                  // cwd
  { FOO: 'bar' },              // env
  false,                       // useShell
)
```

Signature:

```ts
process.spawn(cmd, args?, cwd?, env?, useShell?): Promise<{ procId, pid }>
```

Stdout/stderr arrive as events to the invoking window:

```ts
ow.on('process.stdout', ({ procId, data }) => append(data))
ow.on('process.stderr', ({ procId, data }) => appendError(data))
ow.on('process.exit',   ({ procId, code }) => console.log('exited', code))
```

Write to stdin and terminate:

```ts
await ow.invoke('process', 'write', procId, 'input\n')
await ow.invoke('process', 'kill', procId)            // SIGTERM
await ow.invoke('process', 'kill', procId, 9)         // SIGKILL (number)
await ow.invoke<number[]>('process', 'list')          // live procIds
```

`data` may be a string or `{ b64 }` for binary output.

## PTY (interactive terminal)

A PTY gives the child a pseudo-terminal, so programs behave interactively
(prompts, colors, cursor control). This is the correct backend for a terminal
emulator UI.

```ts
const { procId, pid } = await ow.invoke<{ procId: number; pid: number }>(
  'process', 'ptyOpen',
  '/bin/bash',        // command
  [],                 // args
  '/home/me',         // cwd
  undefined,          // env (or an object)
  80,                 // cols
  24,                 // rows
)

ow.on('process.stdout', ({ procId, data }) => term.write(data))
await ow.invoke('process', 'write', procId, 'ls\n')
await ow.invoke('process', 'ptyResize', procId, 120, 30)   // on window resize
```

Signature:

```ts
process.ptyOpen(cmd, args?, cwd?, ?, cols?, rows?, env?): Promise<{ procId, pid }>
process.ptyResize(procId, cols, rows): Promise<null>
```

`kill` on a PTY closes the master fd, which ends the session.

## Platform notes

- Linux: `forkpty` and `pipe`/`posix_spawn`.
- Windows: ConPTY and `CreateProcess` (verify in CI).
- Commands are passed as an argument array; `useShell: true` (spawn only) runs
  them through the system shell.

## Next steps

- [`process` module reference](../api/modules/process.md).
- [Main process](main-process.md) — Node's `child_process` in the sidecar.
