---
title: app
description: The application object in the main process. Import it from @owear/core.
order: 2
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `app`

The application object in the main process. Import it from `@owear/core`.

```ts
import { app } from '@owear/core'
```

`app` is a plain object exposing methods and an EventEmitter interface. Window
creation must wait for `app.whenReady()`.

## Lifecycle

```ts
app.whenReady(): Promise<void>
// Connects to the kernel control socket. Resolves once the channel is ready.
// Idempotent: repeated calls return the same promise (`readyPromise`).

app.quit(exitCode = 0): Promise<void>
app.info(): Promise<{ pid: number; version: string; socket: string }>
```

## Identity and paths

```ts
app.getName(): string
app.setName(name: string): void
app.getVersion(): string          // package.json version, else the kernel version
app.isPackaged(): boolean
app.getAppPath(): string          // app bundle root

app.getPath(name: string): string
app.setPath(name: string, value: string): void
```

`getPath` names: `home`, `appData`, `userData`, `sessionData`, `cache`, `temp`,
`logs`, `downloads`, `documents`, `desktop`, `pictures`, `music`, `videos`,
`exe`, `appPath`. Unknown names return an empty string.

```ts
const dbFile = path.join(app.getPath('userData'), 'notes.db')
```

## Icon

```ts
app.setIcon(path: string): void
// Default window icon (PNG/JPEG). Applies to windows created afterwards and to
// `new BrowserWindow()` without its own `icon`.
```

## Node runtime

```ts
app.ensureNodeRuntime(range = 'latest'): Promise<{ path: string; version: string; source: string }>
// range: 'latest' | 'lts' | 'v22' | …
// source: 'env' | 'system' | 'cache' | 'downloaded'
```

## Command line

```ts
app.commandLine.appendSwitch(key: string, value?: string): void
app.commandLine.appendArgument(arg: string): void
app.commandLine.hasSwitch(key: string): boolean
app.commandLine.getSwitchValue(key: string): string
```

Applied per WebView when a window is created (Windows:
`AdditionalBrowserArguments`; Linux: best effort).

## Node bridge

```ts
app.handle(fn: string, handler: (...args: any[]) => unknown): () => void
app.handleContext(fn: string, handler: (ctx: { windowId: number }, ...args: any[]) => unknown): () => void
app.send(name: string, payload?: unknown, windowId?: number): Promise<void>
```

See [node-ipc.md](node-ipc.md) and [IPC guide](../../guides/ipc.md).

## Workers

```ts
app.forkWorker(entry: string, options?: ForkWorkerOptions): WorkerHandle
app.workersDir(): string | undefined
```

See [workers.md](workers.md).

## Message channels

```ts
app.createChannel(): { port1: MessagePortMain; port2: MessagePortMain }
app.sendPort(windowId: number, name: string, port: MessagePortMain): Promise<void>
```

See [ports.md](ports.md).

## Custom protocols

```ts
app.protocol(name: string, options?: {
  privileged?: { secure?; cors?; stream?; standard?; fetch? }
  serve?: string
  handler?: ProtocolHandler
}): Promise<void>
```

See [protocol.md](protocol.md).

## Events

```ts
app.on(event, listener)
app.once(event, listener)
app.off(event, listener)
```

Common events:

| Event | Payload |
|---|---|
| `before-quit` | — |
| `will-quit` | — |
| `window-all-closed` | — |
| `second-instance` | `{ argv }` |
| `activate` | — |
| `child-process-gone` | — |

## Other exports

```ts
app.__channel            // the raw ControlChannel (advanced)
export { readyPromise }  // the promise resolved by whenReady()
```
