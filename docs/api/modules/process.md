---
title: process
description: Spawn child processes with piped stdio and open real PTYs for interactive
order: 16
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `process`

Spawn child processes with piped stdio and open real PTYs for interactive
sessions.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

## Functions

| Function | Signature |
|---|---|
| `spawn` | `(cmd, args?, cwd?, env?, useShell?) → { procId, pid }` |
| `write` | `(procId, data) → null` |
| `kill` | `(procId, signal?) → null` |
| `list` | `() → procId[]` |
| `ptyOpen` | `(cmd, args?, cwd?, ?, cols?, rows?, env?) → { procId, pid }` |
| `ptyResize` | `(procId, cols, rows) → null` |

`data` in `write` is a plain string (`'utf8'`) or `{ b64: '…' }` for bytes.

## Pipes

```ts
const { procId, pid } = await ow.invoke('process', 'spawn',
  '/bin/bash', ['-lc', 'ls -la'], '/home/me', { LANG: 'C' }, false)

ow.on('process.stdout', ({ procId, data }) => console.log(data))
ow.on('process.stderr', ({ procId, data }) => console.error(data))
ow.on('process.exit',   ({ procId, code }) => console.log('exit', code))

await ow.invoke('process', 'write', procId, 'input\n')
await ow.invoke('process', 'kill', procId)          // SIGTERM
await ow.invoke('process', 'kill', procId, 9)       // SIGKILL on Unix
```

## PTY

`ptyOpen` gives the child a pseudo-terminal. On Unix, `kill` closes the master
fd, which ends the session.

```ts
const { procId } = await ow.invoke('process', 'ptyOpen',
  '/bin/bash', [], '/home/me', undefined, 80, 24)
ow.on('process.stdout', ({ procId: id, data }) => terminal.write(data))
await ow.invoke('process', 'ptyResize', procId, 120, 30)
```

## Events

| Event | Payload |
|---|---|
| `process.stdout` | `{ procId, data }` |
| `process.stderr` | `{ procId, data }` |
| `process.exit` | `{ procId, code }` |

Events are directed to the window that spawned the process.

## Platform notes

- Linux: `forkpty`, `pipe`/`posix_spawn`.
- Windows: ConPTY and `CreateProcess` (verify in CI).
- `useShell: true` runs `cmd` through the system shell (spawn only, not PTY).

## See also

- [Processes and PTY guide](../../guides/process-and-pty.md)
