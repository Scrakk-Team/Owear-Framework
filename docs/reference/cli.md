---
title: CLI reference
description: The ow command-line tool scaffolds, develops, builds, and packages Owear apps.
order: 1
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# CLI reference

The `ow` command-line tool scaffolds, develops, builds, and packages Owear apps.

```bash
ow <command> [options]
ow --help
```

## `ow create`

```bash
ow create <dir>              # scaffold an app
ow create installer [dir]    # scaffold the installer
ow create uninstaller [dir]  # scaffold the uninstaller (inside ./installer)
```

Copies a template into `dir` and replaces `__APP_NAME__` / `__PKG_NAME__`
placeholders. Fails if `dir` exists and is not empty.

## `ow dev`

```bash
ow dev
```

Runs the development loop: Vite on port 5173, compiles native modules, compiles
`app/main.ts` and workers with esbuild, then launches the kernel. See
[Dev and build](../getting-started/dev-and-build.md).

## `ow build [target]`

```bash
ow build                 # frontend + main.js + workers + modules → dist/
ow build app [--format …]  # distributable artifact → release/
ow build installer         # installer binary → release/
ow build uninstaller       # uninstaller binary → release/
```

`ow build app` formats: `binary` (default), `deb`, `appimage`, `msi`.

Signing flags (app/installer/uninstaller):

```bash
--sign-key <pem>            # detached Ed25519 signature → <file>.sig
--pfx <p12>                 # Authenticode (needs osslsigncode/signtool)
--pfx-password-env <ENV>    # defaults to OW_SIGN_PFX_PASSWORD
--timestamp <url>           # Authenticode timestamping
--require-sign              # fail the build if signing is not performed
```

Installer mode:

```bash
ow build installer --mode minimal   # single-binary payload
ow build installer --mode layout    # directory-tree payload
```

## `ow api`

Only available inside the Owear repository.

```bash
ow api list          # list APIs and their manifests
ow api new <name>    # scaffold api/<name>/ + manifest, then regenerate discovery
```

## `ow update`

Publishes an auto-update (block delta + signed YAML manifest).

```bash
ow update --gen-key owear-signing
ow update --file release/MyApp-1.4.0 --version 1.4.0 \
  --channel latest --url https://up.example.com \
  --key owear-signing.pem --notes @CHANGELOG.md --out-dir release
```

Flags: `--file`, `--version`, `--channel`, `--url`, `--notes`, `--block-size`,
`--mandatory`, `--key`, `--out-dir`.

## Helpful environment variables

| Variable | Used by | Effect |
|---|---|---|
| `OW_KERNEL_BIN` | CLI | Path to the kernel binary |
| `OW_SIGN_KEY` | CLI | Ed25519 private key for signing |
| `OW_SIGN_PFX` | CLI | PKCS#12 for Authenticode |
| `OW_SIGN_PFX_PASSWORD` | CLI | PFX password |

See the full list in [Environment variables](environment-variables.md).

## `owear-build-native`

A companion binary that compiles `native/*.cpp` into `.owm` modules. Used by the
Vite plugin and by `ow dev`/`ow build`; you normally do not call it directly.
`OW_MODULES_OUT` sets the output directory, `OW_INCLUDE_DIR` the framework
headers.
