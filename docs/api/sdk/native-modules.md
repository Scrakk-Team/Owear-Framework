---
title: Native module access
description: Call any loaded native module from the main process, and introspect what is
order: 10
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Native module access

Call any loaded native module from the main process, and introspect what is
loaded. This is the counterpart of the renderer's `ow.invoke`.

```ts
import { invokeNative, listNativeModules, nativeModuleInfo } from '@owear/core'
```

## `invokeNative`

```ts
invokeNative<T = unknown>(module: string, method: string, ...args: unknown[]): Promise<T>
```

Equivalent to `ow.invoke` in the renderer, but with no window involved. This is
the piece that lets you port Electron main-process APIs.

```ts
const text = await invokeNative<string>('fs', 'readText', '/etc/hostname')
const displays = await invokeNative('screen', 'getAllDisplays')
```

> Builtins that operate on a window expect the window id as the **first**
> argument:
> ```ts
> await invokeNative('ow-window', 'setTitle', win.id, 'Hello')
> ```

## `listNativeModules`

```ts
listNativeModules(): Promise<NativeModuleInfo[]>
```

## `nativeModuleInfo`

```ts
nativeModuleInfo(name: string): Promise<NativeModuleInfo>
// rejects if the module does not exist
```

```ts
interface NativeModuleInfo {
  name: string
  version: string
  origin: string        // path to the .owm, or "builtin:<name>"
  builtin: boolean
  functions: number     // count (compat)
  functionNames: string[]
}
```

## Example

```ts
const mods = await listNativeModules()
for (const m of mods) {
  console.log(m.name, m.version, m.builtin ? '(builtin)' : m.origin, m.functionNames)
}
```

These map to the control-protocol commands `module.invoke`, `module.list`, and
`module.info` (see the [control protocol](../../reference/control-protocol.md)).
