---
title: Roadmap
description: Owear roadmap and platform status
order: 2
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Roadmap

Status: F0–F4 are built (verified end to end on Linux; complete Windows
sources pending verification on the target OS).

## F0 — Skeleton ✅

- [x] pnpm monorepo + CMake presets for Linux and Windows
- [x] Public contracts in `include/ow/` (anti-drift via compile error in CI)
- [x] Platform source selection via CMake (`_win`/`_linux` suffixes)
- [x] Unit tests (minjson, codec, sha256) — no gtest, zero deps

## F1 — Window + WebView + Bridge ✅ (Linux) · 🔧 (win)

- [x] Kernel: GTK/Win32 app loop
- [x] Multi-window with a `WindowManager` (`LiveWindows`)
- [x] Full WebKitGTK backend: init scripts, message handler, `app://` scheme
      with anti-path-traversal
- [x] Bridge invoke/event working end to end (JS promises ↔ native)
- [x] WebView2 backend (virtual host `app.owear`, WebMessageReceived)
- [ ] Physical verification on Windows (CI matrix configured)

## F2 — Native modules (.owm) ✅

- [x] Stable C ABI (`ow_module_descriptor`), `OW_MODULE_BEGIN/FN/END` macros
- [x] Loader dlopen/LoadLibrary with `OW_MODULES_DIR` search
- [x] Stock `fs` module (builtin + standalone `.owm`)
- [x] `owear-build-native`: compile an app's `native/*.cpp` → `.owm`
- [x] Codegen of bindings via `@owear/vite-plugin` (`@owear/native` virtual)

## F3 — Bridge performance ✅

- [x] Outbox batching: all `_apply`/`_event` emitted in one eval per tick
- [x] Zero-copy SHM (`ow-shm://`): static mmap regions served, `ow.readShared()`
      → ArrayBuffer. `fs.readFile` uses handles ≥ 256 KB. Verified: 5 MB with
      identical SHA-256 host ↔ renderer.
- [x] Optional synchronous channel (`ow.invokeSync` via `ow-sync://` + XHR;
      reentrancy warning documented)
- [x] `closeRequested` veto from JS/SDK (`requestId` +
      `ow-window.respondCloseRequest` + configurable `OW_CLOSE_TIMEOUT_MS`,
      default 1 s). Verified: veto ✓ allow ✓ timeout ✓
- [x] `closeRequested` veto from the SDK: the window forwards the event over the
      control socket, so the main process can veto with `win.closeRespond`.
      Covered by E2E (`sdk.closeRequested.veto` / `.allow`)
- [x] Native Windows overlay (WM_NCCALCSIZE, Chromium technique; VERIFY IN CI)
- [x] Base64 fallback for small binaries

## F4 — Distribution ✅

- [x] Node Runtime Manager: cascade resolution `OW_NODE_BIN` → SYSTEM Node
      (PATH / OS locations) → cache → official download from nodejs.org +
      SHA256. The chosen version is recorded with its source
      (`env|system|cache|downloaded`); system wins over cache.
- [x] The app main is compiled to JavaScript (`ow dev`/`ow build` with esbuild,
      which ships with Vite): works with any installed Node, because Node does
      not run `.ts` until 22.6.
- [x] Native modules from the main process (`module.invoke` + `module.list` on
      the control socket, `invokeNative`/`listNativeModules` in the SDK): the
      main process uses fs/process/net just like the renderer.
- [x] Node sidecar (fork/exec / CreateProcess) with `OW_CONTROL_SOCKET`
- [x] `ControlServer` UDS/named-pipe (~20 commands)
- [x] `@owear/core` SDK (app, typed BrowserWindow)
- [x] `@owear/cli` (create/dev/build)
- [x] App template + `pnpm dev` flow

## F5 — Scrakk Studio port

Reserved for the author.

---

# Modular APIs (`api/<name>`) — F6/F7/F8/F9 ✅ Linux

Each API lives in `api/<name>/` and compiles to an independent `.owm` (modular
updates). 13 modules + the `ow-window` builtin load and are verified E2E on Linux
(19/19 tests green).

| API | Linux status | Windows |
|---|---|---|
| fs (v2: fd handles, copy/rename/chmod/symlink/lstat/realpath/mkdtemp/access/truncate, **watch** inotify) | ✅ E2E | watch: ReadDirectoryChangesW — VERIFY |
| process (**real PTY Manager** forkpty + spawn pipes + exit/stdout events) | ✅ E2E (interactive bash) | ConPTY — VERIFY |
| path (join/resolve/dirname/basename/extname/normalize + XDG/Known Folders) | ✅ E2E | Known Folders — VERIFY |
| dialog (GtkFileChooserNative / IFileDialog) | ✅ registered (modal not automatable) | VERIFY |
| clipboard (text + PNG image → SHM) | ✅ E2E text | WIC pending |
| screen (monitors/workarea/scale/rotation/label/cursor, **added/removed/changed events**, watch) | ✅ E2E | WM_DISPLAYCHANGE — VERIFY |
| net (native HTTP(S) without CORS + download SHA256; ≥256KB → SHM) | ✅ E2E | same code |
| notification (system: org.freedesktop.Notifications) | ✅ E2E (real id) | toast — pending |
| power (logind sleep/shutdown/lock + UPower + Xss; **idle/battery**, events) | ✅ E2E | PowerBroadcast + WTS — VERIFY |
| shell (openExternal/openPath/showItemInFolder via FileManager1 D-Bus) | ✅ validation+launch | IShellLink — VERIFY |
| updater (**auto-update: block delta + Ed25519 signatures (metadata+binary) + Authenticode + YAML manifest + retries/resume + rollback**) | ✅ E2E update_apply + unit | same |
| menu (optional declarative popup JSON; setApplicationMenu noop on Linux by design) | ✅ | HMENU — pending |
| globalshortcut (X11 XGrabKey; **optional**, Wayland unsupported) | ✅ registered | RegisterHotKey — pending |
| tray (native StatusNotifierItem over GDBus) | ✅ (needs SNI watcher) | Shell_NotifyIcon — pending |

## Kernel-coupled builtins ✅ Linux

| API | Status |
|---|---|
| window-extras (devtools ✓ capturePage PNG→SHM ✓ alwaysOnTop ✓ opacity ✓ flashFrame ✓ setIcon ✓ userAgent ✓ zoom ✓; printToPDF/progressBar → clear error v1) | ✅ E2E 13/13 |
| session (cookies get/set/delete ✓ clearStorage ✓ proxy ✓ downloads with events ✓) | ✅ E2E cookies |
| capturer (getSources thumbnails + captureScreen full → SHM PNG; **X11-only**, Wayland = documented error) | ✅ E2E under Xvfb |
| crashreporter (signals + backtrace to cache/crashes/) | ✅ registered |

Total: **19 modules** (15 `.owm` + 4+1 builtins), 32 base E2E tests green, plus
the verified F-next round:

| F-next extra | Status |
|---|---|
| Navigation (reload/stop/goBack/goForward/canGo*/getURL/getTitle + events to the SDK) | ✅ E2E |
| findInPage/findStop (native FindController + JS `window.find` helper; ⚠ degraded under Xvfb) | ⚠ structural |
| Full net.request (POST/PUT/headers/body/timeoutMs; ≥256KB → SHM) | ✅ E2E POST echo |
| Directed IPC `ow.emitTo(windowId)` window → window | ✅ E2E self+A→B |
| app.setBadgeCount (Unity) / requestSingleInstanceLock / relaunch | ✅ registered |
| session.setUserAgentAll + spellCheck | ✅ registered |
| capturer under Xvfb/X11 | ✅ PNG 1280×800 |

Minor pending: printToPDF (no direct API in WebKitGTK v2.52), tray menus,
webRequest interceptor, Wayland capture (portal).

## CI, Linux and Windows

- **Linux:** build + ctest + headless E2E (Xvfb + dbus/dunst) — GREEN.
- **Windows:** compiles with vcpkg (zlib/openssl). The WebView2 backend is gated
  behind `OW_WITH_WEBVIEW2` (default OFF): without the SDK it compiles a stub
  `CreateWebviewBackend() → nullptr` with a clear log. Enabling it requires the
  `Microsoft.Web.WebView2` NuGet package.
