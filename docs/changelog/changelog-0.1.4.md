---
title: 0.1.4
description: Installer system (D1) improvements and its default template, plus the draw.io port as an example with its own installer.
order: 5
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.4

Improvements to the **installer system (D1)** and its **default template**, plus
the **draw.io** port as an example with its own installer.

## Auto-update

A complete update system with **delta**, **signing** and a **YAML manifest**
(see `docs/UPDATES.md`):

- **`autoUpdater` in the SDK** (`@owear/core`, main process): `setFeedURL`,
  `checkForUpdates()`, `downloadUpdate()`, `quitAndInstall()` and events
  (`checking-for-update`, `update-available`, `update-not-available`,
  `download-progress`, `update-downloaded`, `error`). `generic` and `github`
  providers; channels (`latest`, `beta`, …) and `mandatory`.
- **Block-level delta** (blockmap style): the artifact is split into blocks and
  only the **blocks whose sha256 changes** are downloaded (`Range: bytes=…`),
  reusing the rest of the installed binary. It merges contiguous blocks and
  falls back to a full download when there is no blockmap.
- **Ed25519 signature** of the manifest (over `"<version>:<sha256>"`), verified
  with the bridge public key (`updater.publicKey`); **integrity** sha256+sha512
  of the artifact (and per block while assembling).
- **Native `updater` module**: new `state()` (version, `exe`, `dir`, `mode`,
  platform/arch, reading the installed-apps **registry**) and `apply({ path })`
  (**atomic** binary replace + process **relaunch**).
- **`ow update` command** (+ `tools/owear-update.mjs`): `--gen-key` (Ed25519
  keypair) and channel publishing → `<file>.blockmap` + signed `<channel>.yml`.
  Flags: `--file`, `--version`, `--channel`, `--url`, `--key`, `--notes`,
  `--block-size`, `--mandatory`, `--out-dir`.
- **Tests**: `packages/core/test/updater.test.mjs` (YAML, semver, delta, signature)
  and `updater-feed.test.mjs` (real tool + HTTP `Range` + assembly and
  verification); E2E `sdk.updater.state`.
- **Binary signing** (`tools/owear-sign.mjs`, wired into
  `ow build app|installer|uninstaller`): detached Ed25519 (`<file>.sig`) on any
  platform and **Authenticode** (PE/MSI) via `osslsigncode`/`signtool` when
  `--pfx` is given. Flags `--sign-key`, `--pfx`, `--pfx-password-env`,
  `--timestamp`, `--require-sign` (env `OW_SIGN_*`). The manifest adds
  `binarySig` (Ed25519 signature of the whole artifact), verified by the updater.
- **Retries and resume**: `autoUpdater.maxRetries` / `retryDelay` /
  `requestTimeout`; exponential backoff with jitter on `408/425/429/5xx` and
  network errors; full download **resumable** with `Range: bytes=<received>-`.
- **Rollback**: `updater.apply({ path, backup })` keeps `<exe>.owprev`;
  `updater.rollback()` restores and relaunches; `updater.commit()` confirms and
  deletes the backup; `state().hasRollback`. In the SDK: `autoUpdater.rollback()`,
  `commitUpdate()`, `quitAndInstall({ backup })` and a **boot guard**
  `armBootGuard()` (detects crash loops and reverts). `autoInstallOnAppQuit` and
  `allowPrerelease` are now wired.
- **New tests**: `updater-boot.test.mjs` (crash loop/rollback),
  `updater-sign.test.mjs` (Authenticode + Ed25519) and `updater-retry.test.mjs`
  (retries, `Range` resume, timeout); E2E `tests/e2e/update_apply.py`
  (real apply + backup + rollback + commit over a copy of the kernel).

## Added

- **Installed-apps registry** (an Owear capability, in the `installer` builtin):
  `install()` writes `~/.config/owear/installed/<appId>.json` (Linux) /
  `%APPDATA%\\owear\\installed` (Windows) with `{appId, appName, version, dir,
  mode, installedAt}`; `uninstall()` deletes it. Related API:
  - `installer.info()` includes `installed` and `dir` (merge of `installer.json`
    + registry).
  - `installer.state({ dir? })` — **without `dir`** it uses the registered one.
  - `installer.uninstall({ dir? })` — **without `dir`** it uses the registered one.
  - **`installer.list()`** — every installed app.
- **Template uninstaller**: it finds the installation **through the registry**
  (no path typed in) and uses the native `installer.chooseDir()` (not `dialog`).
- **App icon API**: `BrowserWindow({ icon })` (path to PNG/JPEG) and
  `app.setIcon(path)` (default app icon, applied to windows created afterwards).
  The installer also uses `owear.bridge.ts → app.icon` for the `.desktop`
  (`Icon=`) and **copies the icon into `hicolor`**.
- **The Starter template ships an icon and uses it**: `ow create` now generates
  `public/favicon.svg` (the Owear mark) and `app/main.ts` applies it with
  `app.setIcon(...)` — resolving the path both in dev (`public/`) and packaged
  (`dist/` → `app/`). It is the **base app icon**; the kernel decodes the SVG via
  `window.setIcon` (GdkPixbuf, with SVG support).
- **`installer.defaultDir()`** and **`installer.chooseDir()`** (native in the
  builtin): the installer UI no longer uses `dialog` (an `.owm` module) → the
  installer is **self-contained** (builtins only). `chooseDir` opens the native
  folder picker (GTK on Linux, `IFileOpenDialog` on Windows).
- **Installer template with the Starter styles**: the default installer
  (`packages/cli/template-installer/`) now uses the Owear Starter app tokens and
  components — dark palette + rose accent, **JetBrains Mono** and **Dancing
  Script** (self-hosted, with `@font-face`), titlebar with `brand__mark`, `.btn`
  with an inset hover, `.card`/`.board`/`.console`/`.badge`. It also applies to
  the **uninstaller** sub-template.
- **Installer dev-loop** (`examples/drawio/dev-installer.mjs`): it starts `vite`
  (dev server for the installer UI) and launches the **installer binary**
  pointing it at that server with `OW_DEV_SERVER_URL` → **UI hot-reload** against
  the real `installer` API (binary payload), **without repackaging** the 55 MB.
- **draw.io port + installer** in `examples/drawio/`: the real draw.io webapp on
  Owear, `owear.bridge.ts`, the scaffolded installer template and the installer
  build with the **command** `ow build installer --mode minimal`.

## Changed

- **The default `ow create` template is now the Starter**: generated apps come
  with the Starter UI (dark palette + rose accent, self-hosted JetBrains Mono +
  Dancing Script, custom titlebar, cards/console) and the C1–C10 native-module
  showcase (app, dialog, fs, webContents, nativeImage, menu, tray, nativeTheme,
  print/printToPDF, BrowserWindow, screen, power) + an embedded browser (native
  webview).
- **Installer template without hardcoded emojis**: the install plan shows path +
  size (previously `📁`/`📄`), and the window controls (min/max/close) are wired
  in the renderer.
- `ow build` (production) now **bundles `@owear/core`** into `app/main.js`
  (`prepareMain(..., bundle)`); in `ow dev` it stays external. Required because a
  single-binary/installer payload **does not carry `node_modules`**.

## Fixed

- **Auto-update (audit)**: several bugs found while reviewing the updater were
  fixed:
  - a signed feed (`binarySig`) **broke** `checkForUpdates()` in apps without a
    `publicKey`; now the signature is *best-effort* without a key and mandatory
    with one (consistent with the docs);
  - `request()` **did not release the connection** when retrying a `5xx` (socket
    leak); it now cancels the body before the backoff;
  - a **relative blockmap** was not resolved against the manifest's directory;
  - the installer's internal **uninstaller** was built **without the signing
    flags** (`--sign-key`/`--pfx` were not propagated);
  - `apply()` now **restores the backup** if the relaunch (`execv`) fails, so the
    executable is never left in a state that won't boot;
  - manifest hashes/signatures are always quoted (prevents a "digits-only" value
    from being reinterpreted as a number when parsing the YAML);
  - the delta now emits a final `download-progress` at 100%.

- **Paths with `~`**: `install`/`uninstall`/`verify`/`state`/`shortcuts` now
  **expand `~`** (`$HOME`/`%USERPROFILE%`) and normalize to **absolute** — before,
  a literal `~` folder was created (installing "in the wrong place").
- **Real shortcut (Linux)**: `.desktop` with an **absolute** `Exec`,
  `Categories`/`StartupWMClass`, `chmod 0755`, **`update-desktop-database`**
  (shows up in the app grid), an icon in `hicolor` and `~/Desktop` marked as
  *trusted* (`gio set … metadata::trusted`).
- **Environment when launching the installed app**: opening the app from the
  installer inherited the installer's `OW_*` vars (`OW_ASSETS_DIR`/`OW_APP_MAIN`
  pointed at its cache) and the launched app loaded **the installer UI** instead
  of the app. They are now cleared (`OW_ASSETS_DIR`, `OW_APP_MAIN`,
  `OW_MODULES_DIR`, `OW_APP_WORKERS`, `OW_MODE`, `OW_APP_ID`, `OW_APP_VERSION`)
  before `execv` / `CreateProcessW`.

## Notes

- Verified end-to-end: `ow build installer` produces installer + uninstaller; it
  installs and **the installed app boots with its own payload**.
- Still to polish (in progress): per-file progress, mode picker
  (`minimal`/`layout`) in the UI, app icon, and fine-tuning the layout/window.
