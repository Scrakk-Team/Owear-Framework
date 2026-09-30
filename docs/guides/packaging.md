---
title: Packaging
description: ow build app turns your project into a distributable artifact. It first runs
order: 16
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Packaging

`ow build app` turns your project into a distributable artifact. It first runs
the normal bundle step (`ow build`), then wraps the kernel, your frontend, the
main process, workers, and native modules into a payload.

```bash
ow build app --format binary      # default: one self-contained executable
ow build app --format deb         # Debian/Ubuntu package (Linux)
ow build app --format appimage    # portable AppImage (Linux)
ow build app --format msi         # Windows installer database (Windows)
```

Output goes to `release/`.

## Format: `binary`

The kernel and every app file are packed into a **single executable**. Running
it launches your app; there is nothing else to install.

```bash
ow build app --format binary
# → release/<slug>
```

How it works: the CLI assembles an app bundle (`app/`, `modules/`,
`manifest.json`) and packs it with the kernel using `owear-pack.mjs`. At startup
the kernel detects the embedded payload and serves it through the `app://`
scheme.

## Format: `deb`

Builds a Debian package with:

```text
/opt/<slug>/owear          # kernel
/opt/<slug>/app/           # frontend
/opt/<slug>/main.js        # main process (if any)
/opt/<slug>/workers/       # workers (if any)
/opt/<slug>/modules/       # native modules (if any)
/usr/bin/<slug>            # wrapper that sets env vars and execs the kernel
/usr/share/applications/<appId>.desktop
/usr/share/icons/hicolor/256x256/apps/<appId>.<ext>
```

The wrapper exports `OW_ASSETS_DIR`, `OW_APP_MAIN`, `OW_APP_WORKERS`, and
`OW_MODULES_DIR` before `exec`.

## Format: `appimage`

Builds an AppDir with an `AppRun` launcher and produces a `.AppImage`. Same
layout as `deb`, under `usr/`.

## Format: `msi` (Windows)

Stages the app on Windows and builds an MSI with `owear-msi.mjs`.

## Signing

`ow build app` (and installer/uninstaller builds) accept:

```bash
--sign-key <pem>           # detached Ed25519 signature → <file>.sig
--pfx <p12>                # Authenticode (needs osslsigncode/signtool)
--pfx-password-env <ENV>   # defaults to OW_SIGN_PFX_PASSWORD
--timestamp <url>          # Authenticode timestamping
--require-sign             # fail the build if signing is not performed
```

Environment equivalents: `OW_SIGN_KEY`, `OW_SIGN_PFX`, `OW_SIGN_PFX_PASSWORD`,
`OW_SIGN_TIMESTAMP`. Without signing material the build simply skips signing
(unless `--require-sign`).

## Packaging overrides

`owear.pack.json` in the project root tweaks defaults:

```json
{
  "modes": { "default": "layout" }
}
```

## App metadata (`owear.bridge.ts`)

The installer/packaging metadata comes from the bridge file:

```ts
import { defineBridge } from '@owear/core'

export default defineBridge({
  app: {
    id: 'com.acme.notes',
    name: 'Notes',
    version: '1.2.0',
    publisher: 'Acme',
    icon: 'build/icon.png',
  },
  targets: {
    linux: { layout: 'minimal', format: ['binary'], shortcuts: ['desktop'] },
    win:   { layout: 'layout',  format: ['msi'],    shortcuts: ['startMenu'] },
  },
  order: ['kernel', 'modules:stock', 'app:main', 'app:assets'],
})
```

Even without an installer you can use the bridge to set the app id/name/version
and icon for `deb`/AppImage/MSI output. See [Installers](installers.md) and the
[bridge reference](../api/sdk/bridge.md).

## Next steps

- [Installers](installers.md) — ship an installer and uninstaller.
- [Auto-update](auto-update.md) — publish signed updates.
