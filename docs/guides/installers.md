---
title: Installers
description: An Owear installer is another Owear app that carries your app as an embedded
order: 8
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Installers

An Owear installer is **another Owear app** that carries your app as an embedded
payload. It has its own UI, optionally its own Node sidecar, and talks to the
kernel through the `installer` native API. The same machinery produces an
uninstaller.

## Scaffold

```bash
ow create installer      # creates ./installer
ow create uninstaller    # creates ./installer/uninstaller (inside the installer)
```

The installer references the parent app through `owear.bridge.ts`.

## Build

```bash
ow build installer
ow build uninstaller
```

`ow build installer` performs:

1. builds the app bundle (the payload);
2. builds the uninstaller (if present) and includes it in the payload;
3. builds the installer UI (`installer/` → `ui-dist/`), plus its `app/main.ts`;
4. writes `installer.json` with app metadata and bridge data;
5. assembles the kernel + payload + UI into one binary with an `OWPK1` footer.

Modes: `--mode minimal` (payload is a single app binary) or `--mode layout`
(the app tree is laid out on install). Defaults come from the bridge target for
the current platform.

## The `installer` API

Available inside the installer/uninstaller binary (`OW_MODE=installer` /
`uninstaller`):

```ts
import { installer } from '@owear/core'

await installer.mode()                       // 'app' | 'installer' | 'uninstaller'
await installer.info()                       // metadata from installer.json
await installer.bridge()                     // the embedded bridge
await installer.defaultDir()                 // recommended install dir
await installer.chooseDir({ title })         // native folder picker (null if cancelled)
await installer.payloadList()                // files in the payload
await installer.payloadRead(relPath)         // read one payload file (base64)
await installer.plan({ mode: 'minimal' })    // compute the install plan
await installer.install({ dir, mode })       // perform the install
await installer.uninstall({ dir, keepData? })
await installer.verify({ dir })              // integrity check
await installer.state({ dir? })              // installation state
await installer.list()                       // installed apps registry
await installer.shortcuts({ execPath, iconPath?, desktop?, menu?, startup? })
await installer.launch({ path, args? })      // launch the installed app
await installer.elevate()                    // { elevated: boolean }
```

A minimal install UI:

```ts
import { installer } from '@owear/core'

const dir = await installer.chooseDir({ title: 'Choose install location' })
if (dir) {
  const result = await installer.install({ dir, mode: 'layout' })
  await installer.shortcuts({ execPath: `${dir}/notes` })
  await installer.launch({ path: `${dir}/notes` })
}
```

## Payload modes

| Mode | What gets installed |
|---|---|
| `minimal` | A single app binary |
| `layout` | A directory tree (kernel, app assets, modules, main process, workers) |

## The bridge

`owear.bridge.ts` is the **contract** between app, installer, and uninstaller. It
is pure data (JSON-serializable) so it can be embedded in the installer.

```ts
import { defineBridge } from '@owear/core'

export default defineBridge({
  app: { id: 'com.acme.notes', name: 'Notes', version: '1.2.0', publisher: 'Acme', icon: 'icon.png' },
  targets: {
    linux: { layout: 'minimal', format: ['binary'], shortcuts: ['desktop'], scope: 'user' },
    win:   { layout: 'layout',  format: ['msi'],    shortcuts: ['startMenu'], scope: 'user' },
  },
  order: ['kernel', 'modules:stock', 'app:main', 'app:workers', 'app:assets'],
  protect: { 'app:main': { integrity: true } },
  node: { mode: 'system' },
  hooks: { postInstall: 'postInstall' },
})
```

- `order` — group order when packing/laying out.
- `protect` — per-group `integrity`, `readonly`, `hidden`, `signed` flags.
- `node` — how the installed app resolves Node (`system` | `download` | `embed`).
- `hooks` — **names** of steps implemented by the installer's own `app/main.ts`
  (`preInstall`, `postInstall`, `preUninstall`, `postUninstall`).

See the [bridge reference](../api/sdk/bridge.md) for the full type.

## Registry

Installed apps are recorded per user, so `installer.list()` and `state()` can
find them:

- Linux: `$XDG_CONFIG_HOME/owear/installed/<appId>.json`
- Windows: `%APPDATA%\owear\installed\<appId>.json`

The updater reads the same registry to know the installed directory.

## Next steps

- [Packaging](packaging.md).
- [Auto-update](auto-update.md).
- [`installer` module reference](../api/modules/installer.md).
