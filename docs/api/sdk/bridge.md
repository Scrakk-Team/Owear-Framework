---
title: defineBridge
description: Defines the app ↔ installer ↔ uninstaller contract in owear.bridge.ts. The
order: 4
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `defineBridge`

Defines the app ↔ installer ↔ uninstaller contract in `owear.bridge.ts`. The
bridge is **pure data**: it must be JSON-serializable (no functions), because
`ow build installer` embeds it into the installer binary as `bridge.json`.

```ts
import { defineBridge } from '@owear/core'

export default defineBridge({
  app: { id: 'com.acme.notes', name: 'Notes', version: '1.2.0', publisher: 'Acme', icon: 'icon.png' },
  targets: {
    linux: { layout: 'minimal', format: ['binary'], shortcuts: ['desktop'], scope: 'user' },
    win:   { layout: 'layout',  format: ['msi'],    shortcuts: ['startMenu'], scope: 'user' },
  },
  order: ['kernel', 'modules:stock', 'app:main', 'app:assets'],
})
```

## Types

```ts
type BridgePlatform = 'linux' | 'win' | (string & {})
type BridgeFormat = 'binary' | 'deb' | 'appimage' | 'exe' | 'msi'
type BridgeLayout = 'minimal' | 'layout'
type BridgePreset = 'electron-like' | 'tauri-like' | 'flat' | 'custom'

interface BridgeApp {
  id: string          // stable id / reverse-DNS recommended
  name: string
  version: string
  publisher?: string
  icon?: string       // relative to the payload (png/svg/ico)
}

interface BridgeTarget {
  format?: BridgeFormat[]
  layout?: BridgeLayout
  preset?: BridgePreset
  dir?: string
  shortcuts?: Array<'desktop' | 'menu' | 'startup' | 'startMenu'>
  scope?: 'user' | 'system'
}

interface BridgeNode { mode?: 'system' | 'download' | 'embed' }

type BridgeGroup =
  | 'kernel' | 'node' | 'modules:stock' | 'modules:app'
  | 'app:main' | 'app:workers' | 'app:assets' | 'resources'

interface BridgeProtect {
  integrity?: boolean
  readonly?: boolean
  hidden?: boolean
  signed?: boolean
}

interface BridgeHooks {
  preInstall?: string
  postInstall?: string
  preUninstall?: string
  postUninstall?: string
}

interface OwearBridge {
  bridgeVersion?: number
  app: BridgeApp
  targets: Partial<Record<BridgePlatform, BridgeTarget>>
  order?: BridgeGroup[]
  protect?: Partial<Record<BridgeGroup, BridgeProtect>>
  node?: BridgeNode
  hooks?: BridgeHooks
}
```

## Validation

`defineBridge` validates the object at build time and throws early:

- `app.id` and `app.version` are required;
- `targets` must be an object;
- each `targets.*.layout` must be `minimal` or `layout`;
- the object must be JSON-serializable (this catches accidental functions).

## Hooks

Hook strings are **names** implemented by the installer's own `app/main.ts`, not
functions. The installer decides when to run them.

## Related

- [Packaging](../../guides/packaging.md) and [Installers](../../guides/installers.md).
- [`installer` SDK](installer.md).
