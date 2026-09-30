---
title: autoUpdater
description: Client-side auto-update in the main process. Extends EventEmitter.
order: 3
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `autoUpdater`

Client-side auto-update in the main process. Extends `EventEmitter`.

```ts
import { app, autoUpdater } from '@owear/core'
```

## Configuration

```ts
interface FeedOptions {
  provider?: 'generic' | 'github'   // default 'generic'
  url: string                        // base URL, or owner/repo for github
  channel?: string
  publicKey?: string                 // Ed25519 (PEM or base64 SPKI)
}

autoUpdater.setFeedURL(options: FeedOptions): void
```

```ts
autoUpdater.currentVersion       // app.getVersion()
autoUpdater.channel              // default 'latest'
autoUpdater.allowPrerelease      // default false
autoUpdater.autoDownload         // default true
autoUpdater.autoInstallOnAppQuit // default true
autoUpdater.maxRetries           // default 3
autoUpdater.retryDelay           // default 800 ms
autoUpdater.requestTimeout       // default 60000 ms
```

## Methods

```ts
autoUpdater.checkForUpdates(): Promise<UpdateCheckResult | null>
autoUpdater.downloadUpdate(): Promise<string[]>          // paths to downloaded files
autoUpdater.quitAndInstall({ backup?: boolean }): Promise<void>
autoUpdater.rollback(): Promise<void>
autoUpdater.commitUpdate(): Promise<void>
autoUpdater.armBootGuard({ threshold?: number; healthDelayMs?: number })
  : Promise<'idle' | 'armed' | 'rolled-back'>
```

`quitAndInstall()` does not return on success: the kernel replaces its own binary
and re-execs, dropping the connection. The SDK treats that as success.

`armBootGuard` records a boot; if the new binary starts more than `threshold`
(default 3) times without staying healthy for `healthDelayMs` (default 10000 ms),
it rolls back. Otherwise it commits after the health delay.

## Events

```ts
autoUpdater.on('checking-for-update', () => {})
autoUpdater.on('update-available', (info: UpdateInfo) => {})
autoUpdater.on('update-not-available', (info: UpdateInfo) => {})
autoUpdater.on('error', (err: Error) => {})
autoUpdater.on('download-progress', (p: ProgressInfo) => {})
autoUpdater.on('update-downloaded', (info: UpdateInfo) => {})
```

## Types

```ts
interface UpdateInfo {
  version: string
  notes?: string
  releaseDate?: string
  mandatory?: boolean
  sha256?: string
  sha512?: string
  size?: number
  blockSize?: number
  blockmap?: string
  signature?: string          // Ed25519 over "<version>:<sha256>"
  binarySig?: string          // Ed25519 over the artifact bytes
  file: { url: string; sha256?: string; sha512?: string; size?: number }
}

interface ProgressInfo { total: number; transferred: number; percent: number; bytesPerSecond: number }
interface UpdateCheckResult { updateInfo: UpdateInfo }
interface InstallOptions { backup?: boolean }
interface BootGuardOptions { threshold?: number; healthDelayMs?: number }
```

## Behavior

- Manifest `<channel>.yml` is fetched from the feed and its signature verified
  when a public key is configured.
- **sha256 is always verified**; sha512 and `binarySig` when present.
- Delta downloads use the blockmap; otherwise a full download with resume.
- If `autoDownload` is true, `checkForUpdates` downloads immediately.
- Release notes and `mandatory` flags come from the manifest.

See the [Auto-update guide](../../guides/auto-update.md) for the publishing flow.
