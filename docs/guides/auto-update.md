---
title: Auto-update
description: Owear's updater downloads only what changed (block delta), verifies integrity
order: 1
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Auto-update

Owear's updater downloads only what changed (block delta), verifies **integrity**
(sha256/sha512) and **authenticity** (Ed25519 signatures), and applies the update
with an atomic replacement + relaunch.

The logic lives in the SDK (`autoUpdater`, main process) because that is where
HTTP, `crypto`, and events are available. The native `updater` module does only
the privileged part: `state()` and `apply()` (atomic replace + `execv`).

## Publish an update

```bash
# 1) once: generate an Ed25519 key pair
ow update --gen-key owear-signing
#   → owear-signing.pem       (private, chmod 600)
#   → owear-signing.pub.b64   (public SPKI, base64)

# 2) build the artifact and publish the channel
ow build app --format binary            # → release/Notes-1.4.0
ow update \
  --file release/Notes-1.4.0 \
  --version 1.4.0 \
  --channel latest \
  --url https://up.example.com \
  --key owear-signing.pem \
  --notes @CHANGELOG.md \
  --out-dir release
#   → release/Notes-1.4.0.blockmap
#   → release/latest.yml
```

Upload all three files to your host:

```text
https://up.example.com/latest.yml
https://up.example.com/Notes-1.4.0
https://up.example.com/Notes-1.4.0.blockmap
```

Flags: `--channel <name>`, `--url <base>`, `--notes <text|@file>`,
`--block-size <bytes>`, `--mandatory`, `--key <pem>`, `--out-dir <dir>`.

## The manifest (`latest.yml`)

```yaml
version: 1.4.0
releaseDate: '2026-09-29T10:00:00.000Z'
path: Notes-1.4.0
sha256: 36fd89a6…            # integrity of the whole artifact
sha512: f9902aa2…
size: 12345678
blockSize: 262144            # 256 KiB delta block size
blockmap: https://up.example.com/Notes-1.4.0.blockmap
mandatory: false
signature: hMyb+IVSla…       # Ed25519 over "<version>:<sha256>" (metadata)
binarySig: Vf3k…             # Ed25519 over the artifact bytes (payload)
notes: |
  Various fixes
```

`signature` covers `"<version>:<sha256>"` — this avoids YAML canonicalization
issues and binds version to content. `binarySig` signs the whole artifact for
defense in depth. Without a configured public key the signature is not required,
but **sha256 is always verified**.

## Client usage

```ts
import { app, autoUpdater } from '@owear/core'

await app.whenReady()

autoUpdater.setFeedURL({
  provider: 'generic',                 // or 'github' (owner/repo)
  url: 'https://up.example.com',
  channel: 'latest',
  publicKey: process.env.OW_PUBKEY,    // SPKI base64 or PEM (recommended)
})

autoUpdater.on('checking-for-update', () => {})
autoUpdater.on('update-available', (info) => console.log('available', info.version))
autoUpdater.on('update-not-available', () => {})
autoUpdater.on('download-progress', (p) => console.log(`${p.percent}%`, p.bytesPerSecond))
autoUpdater.on('update-downloaded', () => {})
autoUpdater.on('error', (e) => console.error(e))

await autoUpdater.checkForUpdates()   // with autoDownload=true this downloads too
await autoUpdater.quitAndInstall()    // replace the binary and relaunch
```

### Options

| Property | Default | Meaning |
|---|---|---|
| `channel` | `'latest'` | Channel manifest name (`<channel>.yml`) |
| `allowPrerelease` | `false` | Accept `-beta` style versions |
| `autoDownload` | `true` | Download as soon as an update is found |
| `autoInstallOnAppQuit` | `true` | Apply the downloaded update on quit |
| `maxRetries` | `3` | Retries per request (exponential backoff) |
| `retryDelay` | `800` | Base backoff in ms (doubles each attempt) |
| `requestTimeout` | `60000` | Per-request timeout in ms |

## How delta works

1. `checkForUpdates()` fetches the manifest, verifies the signature, and compares
   versions (semver).
2. `downloadUpdate()` reads the blockmap and hashes the **installed** binary
   (`updater.state().exe`) block by block.
3. Blocks whose hash matches are **reused**; only changed blocks are fetched with
   `Range: bytes=…`, grouping contiguous blocks into ranges.
4. The artifact is reassembled, `sha256`/`sha512` are verified, and `binarySig`
   if present.

If the server does not support ranges or the blockmap is invalid, it falls back
to a full download with **resume** (using `Range: bytes=<received>-`).

## Rollback and boot guard

Keep a backup of the previous binary and recover automatically if a new build
crash-loops:

```ts
app.whenReady().then(() => {
  const state = await autoUpdater.armBootGuard({ threshold: 3, healthDelayMs: 10_000 })
  // 'idle' | 'armed' | 'rolled-back'
})
```

- `quitAndInstall({ backup: true })` (default) copies the current binary to
  `<exe>.owprev` before replacing it.
- `armBootGuard` records each boot. If the new binary starts more than
  `threshold` times without staying healthy for `healthDelayMs`, it rolls back.
- On a healthy boot it calls `commitUpdate()` (deletes the backup).
- Call `autoUpdater.rollback()` manually to revert while a backup is pending.

## Native `updater` module

The privileged primitives, also callable with `invokeNative`:

```ts
updater.state()                 // { version, exe, dir, mode, platform, arch, hasRollback }
updater.apply({ path, backup? })// atomic replace + relaunch (no return on success)
updater.rollback()              // restore previous + relaunch
updater.commit()                // delete backup
updater.checkForUpdates(feedUrl, currentVersion)   // legacy JSON manifest flow
updater.downloadUpdate()
updater.installAndRelaunch()
```

## Next steps

- [Packaging](packaging.md) and [Installers](installers.md).
- [`autoUpdater` reference](../api/sdk/auto-updater.md).
