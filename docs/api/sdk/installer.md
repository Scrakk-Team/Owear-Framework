---
title: installer
description: The SDK around the native installer module. Only available in binaries built in
order: 7
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `installer`

The SDK around the native `installer` module. Only available in binaries built in
installer/uninstaller mode (`OW_MODE=installer` or `uninstaller`).

```ts
import { installer } from '@owear/core'
```

## Types

```ts
type InstallerMode = 'app' | 'installer' | 'uninstaller'

interface PayloadEntry { rel: string; dst: string; size: number; dir: boolean }
interface InstallerInfo { appId?; appName?; version?; mode?; publisher?; icon?; [k: string]: unknown }
interface PlanOptions { mode?: 'minimal' | 'layout'; layout?: 'flat' | 'tree'; order?: string[] }
interface InstallOptions extends PlanOptions { dir: string; uninstaller?: string; publisher?: string }
interface InstallResult { installed: boolean; dir: string; mode: string; files: number }
interface UninstallResult { removed: number; dir: string }
interface VerifyResult { ok: boolean; mismatches: string[] }
interface StateResult { installed: boolean; version?; mode?; app?; dir? }
interface ShortcutOptions {
  execPath: string; iconPath?: string; appId?: string; appName?: string
  desktop?: boolean; menu?: boolean; startup?: boolean
}
interface LaunchOptions { path: string; args?: string[] }
```

## Methods

```ts
installer.mode(): Promise<InstallerMode>
installer.info(): Promise<InstallerInfo>
installer.bridge<T>(): Promise<T | null>
installer.defaultDir(): Promise<string>
installer.chooseDir(opts?: { title?; defaultPath? }): Promise<string | null>
installer.payloadList(): Promise<PayloadEntry[]>
installer.payloadRead(path): Promise<{ path: string; data: string; size: number }>
installer.plan(opts?: PlanOptions): Promise<PayloadEntry[]>
installer.install(opts: InstallOptions): Promise<InstallResult>
installer.uninstall(opts: { dir: string; keepData?: boolean }): Promise<UninstallResult>
installer.verify(opts: { dir: string }): Promise<VerifyResult>
installer.state(opts?: { dir? }): Promise<StateResult>
installer.list(): Promise<Array<Record<string, unknown>>>
installer.shortcuts(opts: ShortcutOptions): Promise<boolean>
installer.launch(opts: LaunchOptions): Promise<boolean>
installer.elevate(): Promise<{ elevated: boolean }>
```

## Notes

- `chooseDir` uses a native folder picker that does not depend on `.owm` modules.
- `plan` computes the install plan without writing anything.
- `manifest`-based uninstall: `uninstall` reads the manifest saved in `dir`.
- Installed apps are recorded in a per-user registry that the updater also reads.
  See [Installers](../../guides/installers.md).
- Platform support: Linux and Windows currently.
