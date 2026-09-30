---
title: 0.1.3
description: Windows window fix (blank / "not responding"), the Linux → Windows cross-compilation toolchain, titleBarOverlay, a Node bridge, and the C1–C10 app shell.
order: 4
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.3

Fixes the Windows window (blank / "not responding"), adds the Linux → Windows
cross-compilation toolchain, **`titleBarOverlay`** (native window buttons inside
the custom titlebar) on Linux, and a **Node bridge** to expose Node to the UI
without IPC by default. It also adds **Block A of execution/IPC**: Node workers
with a channel, `windowId` in handlers, targeted `webContents.send` and
`MessageChannel`/`MessagePort`. Plus **Block B**: `protocol` (custom schemes),
`safeStorage` (DPAPI / AES-GCM), `theme` (nativeTheme) and `session` permissions;
and the Node sidecar now dies with the kernel. And **Block C (app shell)**: **C1**
full `app` (paths/identity/`commandLine`/events), **C2** full `dialog`, **C3**
`webContents` (objects + events + `capturePage`), **C4** `nativeImage` (PNG codec
with no deps), **C5** `Menu`/`MenuItem`, **C6** `Tray`, **C7** `nativeTheme`,
**C8** `print`/`printToPDF`, **C9** full `BrowserWindow` (options/state/geometry/
events) and **C10** `screen`/`Display` (multi-monitor + events) and
`powerMonitor` (power).

## Added

- **Linux → Windows cross-compilation**: `clang-cl` + `lld-link` with the
  MSVC/Windows SDK fetched by `xwin` (`cmake/windows-cross.cmake`). Produces
  `owear.exe` + modules from Linux, with no Visual Studio and no CI. The `.exe`
  is built with a **static CRT** (`/MT`) and static deps → **no VCRUNTIME or
  extra DLLs**.
- `OW_WITH_OPENSSL` option (default ON): with `OFF` the kernel uses a TLS-less
  HTTP stub (`Runtime/Http_nossl.cpp`) and omits the `net`/`updater` modules.
  Meant for the development cross-build.
- `tools/windows-cross/build-deps.sh` (cross-build of zlib), `build.sh`,
  `serve.sh` and `tools/windows/dev-pull.ps1` (push to Windows over HTTP).
- **`titleBarOverlay` — native buttons inside the custom titlebar (Linux + Windows)**:
  - API: `titleBarOverlay: true | { height, color, symbolColor, buttonColor }` in
    `BrowserWindowOptions` and `win.setTitleBarOverlay(...)`; control command
    `window.setTitleBarOverlay`. The reserved gap is exposed to the renderer as
    `window.__owTitlebarOverlay = { enabled, height, width }`.
  - On Linux they are the **theme's** buttons (`titlebutton` class, freedesktop
    symbolic icons): size, style, hover and spacing come from the distro (nothing
    hardcoded). They are overlaid with a `GtkOverlay`.
  - Glyph color (`symbolColor`), band background (`color`) and **inner circle
    background** (`buttonColor`) are configurable; by default, the theme's.
  - **Theme window shadow** via CSD (empty 0px titlebar) and **rounded corners**
    on the web content (`clip-path` + `border-radius`) with the **radius read from
    the theme** (`decoration { border-radius: N }`).
  - **Resize on Wayland**: `GtkEventBox` zones on edges/corners (the GDK filter is
    X11-only, it doesn't fire on Wayland).
  - **Windows (WebView2): WORKS NOW.** The min/max/close buttons are **drawn by
    hand with GDI+**, identical to Electron (`windows_icon_painter.cc`): icon
    **10px**, min/max/restore **without anti-aliasing** and a 0.5-inset rect,
    restore = **two 8px squares offset by 2**, close = **X with AA**. They are
    drawn in a **top-level layered** popup (`UpdateLayeredWindow`) with an
    **alpha=1** background (imperceptible) so the **hit-test** covers the whole
    rect → exact hover/press. Real transparent background (the titlebar shows
    through).
- **Node bridge (`node`)** — the renderer can **use Node without IPC by default**:
  - `ow.invoke('node', 'call', { fn, args })` runs a main-process handler
    registered with `app.handle(fn, handler)`; the main replies and can push
    events with `app.send(name, payload?, windowId?)` → `ow.on(name)`.
  - **Asynchronous** resolution (the kernel forwards `node.request` to the main
    over the control socket; the main answers with `node.respond`). Modularized
    with the `api/node/owear.module.json` manifest (builtin `node`, `call`
    function).
  - **Opt-in**: hot paths (fs, terminal/PTY…) still go straight
    renderer → kernel → native module, with no Node in between. Meant to expose
    Node to the UI (e.g. VS Code's extension host).
- **Block A — execution and IPC** (workers, context, webContents, MessagePort):
  - **Node workers with a channel (`app.forkWorker`)** — a replacement for
    `utilityProcess.fork`: the main process (already real Node) spawns a child
    with an IPC channel (`child_process.fork`) and a **`process.parentPort`
    shim** so workers ported from Electron work **unchanged**. CLI:
    `app/workers/**` is compiled to `workers/` and exposed in `OW_APP_WORKERS`
    (`ow dev`/`ow build`); `app.workersDir()`. API:
    `postMessage/on(message|exit)/kill/pid`.
  - **`windowId` in Node handlers** — the kernel propagates the source window to
    the main and the SDK exposes it with `app.handleContext(fn, (ctx, ...args) => …)`
    (`ctx.windowId`). `app.handle` is unchanged (additive).
  - **Targeted `webContents.send`** — `win.webContents.send(name, payload)` in the
    SDK (Electron parity) sends ONLY to that window; the renderer receives it with
    `ow.on(name, cb)`.
  - **`MessageChannel`/`MessagePort` (`app.createChannel`)** — a bidirectional
    main ↔ renderer channel without registering per-call handlers:
    `app.sendPort(windowId, name, port)` transfers one end; in the renderer
    `ow.port(id)` (bridge) receives it with `ow.on(name, ({ port }) => …)`. Routed
    by the bridge; the zero-copy binary transport kernel→renderer is still
    `ow-shm://`.
  - **N-API native modules**: documented in `docs/NATIVE.md` (real Node runtime →
    N-API addons and Node prebuilds load as-is; Electron binaries must be rebuilt
    for Node).
  - **Tests**: `@owear/core` now uses `node --test` (`packages/core/test`:
    `forkWorker` round-trip/exit and `createChannel` main↔main). Also verified E2E
    on Linux (Xvfb): worker, `windowId`, `webContents.send` and the
    renderer↔main port.
- **Block B — protocol / data / OS** (`protocol`, `safestorage`, `theme`, `session`):
  - **`protocol` (custom schemes)** — the main registers a scheme and the kernel
    serves it with a **main-process handler** or a **directory**:
    `app.protocol(name, { privileged, serve, handler })`. The handler may return a
    `Response`, `{status, headers, body}` or a `string`. Linux: dynamic
    registration in WebKitGTK with async `finish`; Windows:
    `WebResourceRequested` + deferral. Verified on Linux (handler and `serve`).
  - **`safeStorage`** (`api/safestorage` module) — `isAvailable`, `encrypt` →
    `{data, encrypted}`, `decrypt` (accepts plain text). **Windows: DPAPI**;
    **Linux: AES-256-GCM** with a local 0600 key in the app data dir. SDK:
    `ow.safeStorage`. Verified on Linux (round-trip + key persistence).
  - **`theme` (nativeTheme)** (`api/theme` module) — `get`/`isDark`/`setSource`
    (system|light|dark)/`watch`/`unwatch` + the `theme.changed` event. **Linux:
    GSettings `color-scheme` + GTK theme**; **Windows: the `AppsUseLightTheme`
    registry** (watch by polling). **Forcing the content**: `theme.setSource` →
    kernel `window.setColorScheme` → **Windows WebView2 `PreferredColorScheme`**
    (changes the real `prefers-color-scheme`); on **Linux WebKitGTK does not expose
    forcing it** (the app reacts to `theme.changed`). SDK: `ow.theme` (+ alias
    `ow.nativeTheme`). Verified on Linux (read/events); Windows compiles.
  - **`session` — WebView permissions** — `session.onPermissionRequest(handler)`
    (denies if there is no handler). `PermissionBroker` in the kernel + the
    `session.setPermissionHandler`/`session.respondPermission` commands. **Linux:
    WebKitGTK's `permission-request` signal**; **Windows: `add_PermissionRequested`
    with deferral**. Verified on Linux (geolocation).
  - **`session` — partitions (profiles)** — `session.fromPartition(name)` +
    `BrowserWindow({ session })` isolate cookies/localStorage/IndexedDB/cache per
    profile. Linux: a data dir per partition (`…/webkit/<partition>/{data,cache}`);
    Windows: a profile per partition (`…\owear\WebView2\<partition>`). Verified on
    Linux: `persist:a` and `persist:b` do not share `localStorage`; two windows of
    the same partition do.
  - **`session` — `webRequest`** (`onBeforeRequest`: cancel/redirect) —
    `webRequest.onBeforeRequest({ urls }, handler)`. Windows: **all requests**
    (`WebResourceRequested` + deferral). Linux: **navigations** (`decide-policy`);
    WebKitGTK 2.52 **no longer exposes `send-request`**, so subresources cannot be
    intercepted on Linux (documented). Verified on Linux (navigation blocking).
  - Pending for session (roadmap): `webRequest.onHeadersReceived` and (Linux)
    subresources — blocked by the WebKitGTK API.
- **Sidecar lifecycle** — the Node process **dies with the kernel**:
  Linux/macOS `PR_SET_PDEATHSIG(SIGTERM)` + `NodeManager::ShutdownSidecar()`
  (SIGTERM and, if it doesn't exit, SIGKILL); Windows Job Object
  `KILL_ON_JOB_CLOSE` + `TerminateProcess`. Verified on Linux: after `app.quit()`
  the sidecar PID is gone.
- **`docs/extra/probar-en-starter.md`** — a living guide (WIP) on how to test each
  system in the starter (Linux + Windows), to be filled in over time.
- **Block C — app shell** (started with C1):
  - **Full `app`** — `getPath`/`setPath` (home, appData, userData, temp, cache,
    logs, downloads, documents, desktop, pictures, music, videos, exe, appPath),
    `getName`/`setName`, `getVersion` (from `package.json`), `isPackaged`,
    `getAppPath`, **`commandLine`** (`appendSwitch`/`appendArgument`/
    `getSwitchValue`/`hasSwitch`, applied when creating the WebView: Windows
    `AdditionalBrowserArguments`; Linux best-effort) and **events**
    (`window-all-closed`, `before-quit`, `will-quit`, `second-instance`,
    `child-process-gone`). Kernel: extended `app.info` (`name/appPath/exePath/
    packaged`), `app.setName`, `app.commandLine.*`. SDK: `app` is an EventEmitter.
    Verified on Linux (paths, identity, `commandLine` and the 3 events); Windows
    compiles.
  - **Full `dialog` (C2)** — `showOpenDialog` (`properties`:
    `openFile`/`openDirectory`/`multiSelections`/`showHiddenFiles`, `filters`,
    `defaultPath`, `buttonLabel` → `{ canceled, filePaths }`), `showSaveDialog`
    (`filters`, `defaultPath` → `{ canceled, filePath }`) and `showMessageBox`
    (`type`, `message`/`detail`, `buttons`, `defaultId`, `cancelId`,
    `checkboxLabel` → `{ response, checkboxChecked }`). Linux: GtkFileChooser +
    GtkMessageDialog (with checkbox); Windows: `IFileOpenDialog`/`IFileSaveDialog`
    + `TaskDialogIndirect`. SDK: `ow.dialog`. Registered on Linux (5 functions);
    Windows compiles. (`open`/`messageBox` are kept.)
  - **`webContents` (C3)** — `win.webContents` is an Electron-style object:
    `send`, `capturePage()` (PNG → `{toPNG(), toDataURL()}`; the kernel returns
    base64 with `{base64:true}`), `loadURL`, `reload`, `openDevTools`, `getURL`,
    `getTitle`, `executeJavaScript`, and **`setWindowOpenHandler`** (allow/deny
    `window.open`). **Events** with Electron names: `did-finish-load`,
    `did-fail-load`, `did-start-navigation`, `did-navigate`, `page-title-updated`,
    **`before-input-event`** (keyboard: WebKitGTK `key-press/release-event`;
    WebView2 `AcceleratorKeyPressed`). Kernel: an event sink in the backend + a
    popup broker (`WindowOpenBroker`) + `capturePage` `{base64}`. Verified on
    Linux (`did-finish-load`, `capturePage`, `getURL`); Windows compiles.
  - **`nativeImage` (C4)** — no native dependencies: a **PNG codec** in the SDK
    (Node `zlib`) supporting colorType 0/2/3/4/6 and bitDepth 1/2/4/8/16.
    `nativeImage.createFromPath`/`createFromBuffer`/`createFromDataURL`
    (synchronous like Electron) + `getSize`, `isEmpty`, `toPNG`, `toDataURL`,
    `resize` (bilinear), `crop`. JPEG: `getSize` (SOF) and `toJPEG` (if the source
    is already JPEG). `webContents.capturePage()` now returns a `NativeImage`.
    Verified (SDK tests).
  - **`Menu`/`MenuItem` (C5)** — polished Electron-style API in the **main**:
    `Menu.buildFromTemplate([...])`, `setApplicationMenu`/`getApplicationMenu`,
    `menu.popup({ window, x, y })`. `MenuItem`: `id`, `label`, `role` (quit,
    minimize, close, reload, toggleDevTools, undo/cut/copy/paste/selectAll…),
    `type` (normal/separator/submenu/**checkbox**/**radio**), `checked`, `enabled`,
    `visible`, `accelerator` (display), `submenu`, `click(item, window)`. **Clicks
    go to the main** (`click` handlers + roles). Kernel: a shared template
    (`ow/Menu.hpp`) with types/states; Linux GTK (`GtkCheck/RadioMenuItem`) and
    Windows `HMENU` (popup `TrackPopupMenu` + **menubar** via
    `window.setApplicationMenu` + `WM_COMMAND`). SDK tests; Windows compiles.
  - **`Tray` (C6)** — full tray icon (Tauri/Electron intermediate API):
    `new Tray(image?)`, `setImage`, `setPressedImage`, `setToolTip`, `setTitle`,
    `setContextMenu(Menu)`, `popupContextMenu`, `destroy`, and `click`/
    `right-click`/`double-click` events. Icon = `NativeImage` or a path → PNG
    base64 to the kernel. **Linux: native SNI (GDBus)** —
    `org.kde.StatusNotifierItem` + `com.canonical.dbusmenu` implemented by hand
    (no libraries), registered with the `StatusNotifierWatcher` (GNOME/Zorin with
    the extension, KDE, XFCE+plugin) → `docs/extra/GNOME.md`.
    **Windows: `Shell_NotifyIcon`** with a context menu (`TrackPopupMenu`), events
    and **PNG→HICON** via GDI+. The menu reuses the C5 template. Verified on Linux
    (registration + lifecycle); Windows compiles.
  - **`print` / `printToPDF` (C8)** — `win.webContents.print()` (system dialog)
    and `win.webContents.printToPDF()` (→ PDF `Buffer` via the async
    `window.printToPDF` command). **Windows: WebView2 `PrintToPdf` +
    `ShowPrintUI`** (vector). **Linux: `print`** with `WebKitPrintOperation`
    (dialog) and **`printToPDF`** via a **full-page snapshot → Cairo PDF**
    (rasterized; WebKitGTK exposes no PDF API, and GTK's "Print to File" backend
    blocks). Verified on Linux (`%PDF`, ~2.4 KB); Windows compiles.
  - **Full `BrowserWindow` (C9)** — options: `parent`, `modal`, `transparent`,
    `backgroundColor`, `movable`, `minimizable`, `maximizable`, `closable`,
    `fullscreenable`, `skipTaskbar`, `alwaysOnTop`, `hasShadow`,
    `min/maxWidth/Height`, `aspectRatio`, `show`. Methods: getters (`isVisible`,
    `isFocused`, `isResizable`, `isMovable`, `isMinimizable`, `isMaximizable`,
    `isClosable`, `isAlwaysOnTop`, `isKiosk`, `isDestroyed`, `isFullScreen`),
    setters (`setResizable/Movable/Minimizable/Maximizable/Closable`,
    `setAlwaysOnTop(flag,level)`, `setSkipTaskbar`, `setHasShadow`, `setKiosk`,
    `setIgnoreMouseEvents`, `setProgressBar`, `setBackgroundColor`, `moveTop`,
    `setAspectRatio`), geometry (`getContentBounds/Size`, `setContentSize`,
    `get/setMinimumSize`, `get/setMaximumSize`) and the **statics**
    `getAllWindows`, `getFocusedWindow`, `fromId`. **Events** with Electron names
    (dashed): `enter-full-screen`, `leave-full-screen`, `always-on-top-changed`,
    `page-title-updated`, `show`, `hide`, `restore`, `minimize`, `resized`,
    `moved` (+ the earlier camelCase ones). Linux (GTK) and Windows (Win32: styles,
    `SetWindowPos`, `ITaskbarList3`, `WS_EX_TRANSPARENT`, `WM_SIZING`/`WM_GETMINMAXINFO`).
    Verified on Linux; Windows compiles.
- **Embedded webviews (`webview`, Linux)** — each window can have N child
  WebViews, **each with its own process**, embedded and controllable via API:
  - Builtin `webview` (`api/webview/owear.module.json`, Linux + Windows): `create, destroy,
    setBounds, load, back, forward, reload, stop, canBack, canForward, getURL,
    getTitle, eval, setVisible, setZoom, devtools, findInPage, findStop`.
  - Renderer: `ow.invoke('webview', 'create', { url, x, y, width, height })` +
    `ow.on('webview.loadChanged|urlChanged|titleChanged|loadFailed')` events.
  - Linux (WebKitGTK): each child in its own overlay container (it only
    intercepts its rectangle) + a shared WebKit context with a per-app data dir
    and **one WebProcess per view**; the base for Windows/macOS.
  - Windows (WebView2): each child in its **own child HWND** + a WebView2
    controller parented there (reliable z-order/bounds); an environment with its
    own user data dir. `titleBarOverlay` on Windows **draws the Win10/11-style
    buttons** (GDI+, layered window: only glyphs/hover over the titlebar) and
    exposes `__owTitlebarOverlay` to the renderer.
  - **Automatic focus**: children **do not steal focus while loading** (they start
    with `can_focus=FALSE`); a click on a child gives it focus and a click outside
    the children returns it to the main one.
- `examples/starter`: **redesign in progress** (WIP) — fonts, a container-based
  layout and use of the new APIs (`titleBarOverlay`, embedded browser).
- **Block C — C10 `screen`/`Display` + `powerMonitor` (multi-monitor and power)**:
  - **`screen` (module, Linux + Windows)**: `getAllDisplays`, `getPrimaryDisplay`,
    `getCursorScreenPoint` with a **full Display** (`id`, `bounds`, `size`,
    `workArea`, `workAreaSize`, `scaleFactor`, `rotation`, `internal`, `label`,
    `displayFrequency`, `colorDepth`, `depthPerComponent`, `colorSpace`,
    `monochrome`, `touchSupport`, `accelerometerSupport`, `detected`,
    `nativeOrigin`), a **stable id** (Windows: hash of `szDevice`; Linux: from
    `GdkMonitor*`) and `screen.added` / `screen.removed` / `screen.changed`
    **events** with `watch`/`unwatch`. Windows: a hidden top-level window on its
    own thread for `WM_DISPLAYCHANGE`; Linux: `GdkDisplay` signals and
    `notify::geometry|workarea|scale-factor`.
  - **`power` (module, Linux + Windows)**: `monitorStart`/`monitorStop` and
    `power.suspend`/`resume`/`shutdown`/`lock`/`unlock`/`ac`/`battery` events;
    `idleTime`, `idleState` and `isOnBattery`; `inhibitStart`/`inhibitStop`
    inhibitors. Linux: logind (`PrepareForSleep`/`PrepareForShutdown`/`Session
    Lock/Unlock`) + UPower (with a `/sys` fallback) + X11 Xss (`dlopen`, Wayland →
    `unknown`). Windows: a hidden window with `WM_POWERBROADCAST`/`WM_ENDSESSION` +
    `WTSRegisterSessionNotification` (lock/unlock) + `GetLastInputInfo` +
    `GetSystemPowerStatus`.
  - **SDK**: `screen` (EventEmitter: `added`/`removed`/`changed`, `getAllDisplays`,
    `getPrimaryDisplay`, `getCursorScreenPoint`, `getDisplayNearestPoint`,
    `getDisplayMatching`, `screenToDipPoint`/`dipToScreenPoint` + aliases),
    `powerMonitor` (EventEmitter: `suspend`/`resume`/`shutdown`/`lock`/`unlock`/
    `ac`/`battery`, `getIdleTime`, `getIdleState`, `isOnBatteryPower`,
    `onBatteryPower`) and `powerSaveBlocker` (`start`/`stop`/`isStarted`). Lazy
    watch on first use. Module events are re-emitted by name on the channel
    (`app.__channel.on('screen.added', …)`).
- Dev: `OW_TITLEBAR_OVERLAY[=_HEIGHT]` to test the overlay with `OW_DEMO=1`.
- A build marker in the kernel log (`OWEAR KERNEL BUILD ...`) and
  `PCreate: style=... custom=...` for diagnostics.

## Fixed

- **Blank / "not responding" window on Windows**: the WebView2 controller
  **was not resized** on `WM_SIZE` (it kept invalid bounds). It now calls
  `Resize` on every `WM_SIZE`.
- `WM_NCCALCSIZE` with a custom titlebar now applies the **frame insets**
  (the Electron technique) instead of returning `0` outright, which broke the
  edge hit-testing on Windows 10.
- **DPI awareness** (`PER_MONITOR_AWARE_V2` before creating windows).
- **Modules loaded twice**: `ModuleLoader::SearchPaths` deduplicates
  `OW_MODULES_DIR` vs `<exe>/modules`.
- WebView2 lifecycle diagnostics: `environment → controller → navigate`
  (URL, bounds, `nav completed`, `ProcessFailed`) and `put_IsVisible(TRUE)`.
- **Windows 10: the native caption still showed** with a custom titlebar. It now
  keeps `WS_OVERLAPPEDWINDOW` (so Win11 corners/shadow aren't lost) and the
  caption is removed with `WM_NCCALCSIZE` re-applied with `SWP_FRAMECHANGED` (the
  first `WM_NCCALCSIZE` runs inside `CreateWindowEx`, before the window state
  exists).
- **Linux: ghost gap** above the content (the theme applies its `min-height` to
  the empty CSD `.titlebar` class) → cancelled with CSS in the titlebar's own
  context.
- **Linux: the content rounding wasn't visible** → the `body` background
  propagates to the canvas and is painted square; the root is now clipped with
  `clip-path: inset(0 round Npx)` (N = theme radius).
- **Linux: the overlay colors didn't apply** → in GTK3 a style provider on a
  widget only affects that widget (not its children); it is now registered at the
  screen level with per-`id` selectors.
- **Linux: WebView storage isolated per app** → the WebKitGTK backend used the
  default `WebsiteDataManager` (shared between apps), so
  `localStorage`/`IndexedDB`/cache could mix. It now uses a data manager with a
  **per-app** base dir (`$XDG_DATA_HOME/owear/<app-id>/webkit/{data,cache}`) and
  registers `app://` as a **secure + CORS** scheme (stable origin).
- **Slow close on Linux and Windows**: the veto flow (`closeRequested`) always
  waited for `OW_CLOSE_TIMEOUT_MS` (~1s) even when the app wasn't listening. Now,
  if the renderer has **no listener** for `closeRequested`, it answers `allow`
  **instantly** → the window disappears immediately. With a listener, the veto
  keeps its timeout.
- **Windows: `dialog.dll` did not load** (`STATUS_ENTRYPOINT_NOT_FOUND` / err
  182): it imported `TaskDialogIndirect` (comctl32) statically and, without a v6
  manifest, the whole DLL failed. It is now resolved **dynamically**
  (`GetProcAddress`) and falls back to `MessageBox`.
- **Windows: stale / double-loaded modules** → `ModuleLoader` now prefers
  `<exe>/modules` (same build as the kernel) and **deduplicates by name**, so an
  old runtime package in `OW_MODULES_DIR` no longer wins (no more "unknown
  function").
- **Dev in the monorepo: stale modules** → the CLI (`stockModulesPath`) prefers
  the **local build** over the prebuilt runtime package (a git-ignored artifact
  that may be old).
- **Windows: `capturePage` crashed** (`ACCESS_VIOLATION`): the synchronous
  `capturePage` did a nested message *pump* that re-entered. Capture is now
  **asynchronous** via a control command (`window.capturePage`), with no nested
  loop.
- **Linux: `menu.popup`** gave `Gtk-CRITICAL: no trigger event` (the popup
  arrives from an async click, with no GDK event): it is now placed with
  `gtk_menu_popup_at_rect` over the root window with a **synthetic trigger
  event**.
- **Orphan Node sidecar**: the main process's Node process now **dies with the
  kernel** (`PR_SET_PDEATHSIG` on Linux/macOS + `ShutdownSidecar`; Job Object
  `KILL_ON_JOB_CLOSE` on Windows), with room to deliver `before-quit`/`will-quit`
  before terminating it.
- **Windows: hang/crash on repeated theme changes** → a **data race** in the
  `theme` module (`g_source`, a `std::string`, written by the main and read by
  the `watch` thread) → now guarded with a **mutex**. And `window.setColorScheme`
  was **after** the block requiring `windowId` (which the SDK doesn't pass) → it
  **never applied** `PreferredColorScheme`; moved → it now really changes the
  actual `prefers-color-scheme`.
- **`BrowserWindow.getAllWindows`/`getFocusedWindow` were local to the SDK** →
  now **synchronous** and resolved from the SDK's registry (focus via
  `focus`/`blur` events), like Electron. They previously used new kernel commands
  that **hung the window on Windows** (the starter's "Ventanas" button).
- **Windows: hang with fast clicks (high load)** — two causes:
  1. The control pipe called `CancelIoEx(pipe, nullptr)` when queueing each
     response, which cancels **all** I/O on the handle, **including in-flight
     writes** from the reader thread → truncated responses that **desync the
     SDK's protocol** (an `invoke` that never resolves = a hung window). Also, if
     the reader wasn't yet blocked in `ReadFile`, the response was stranded. The
     reader now **polls** with `PeekNamedPipe` (drains the outbox in ~1 ms) and
     only writes when there is no pending read.
  2. The main loop queued **one `kWmOwPump` per callback** → a burst of N
     callbacks produced N `GetMessage` passes (N-1 empty). There is now **a
     single pump in flight** (`atomic` + re-arm after draining).

## Optimization (Owear vs Electron vs Tauri benchmarks)

Measured with our own harness (`benchmarks/`). Performance changes:

- **Bridge without `eval` — RPC over the `ow-rpc://` scheme**: the renderer calls
  native modules (`.owm`) with `fetch` (args in the POST body, response in the
  body) → removes the request `postMessage` **and** the `executeJavaScript`
  response (the engine no longer compiles code per call). Linux and Windows
  (WebView2). Sequential IPC ~2×, concurrent ~4-5×, 5 MB payload ~1.5×.
- **Large payload over SHM** (`_applyShm` + `ow-shm://`) on the postMessage path.
- **Span-scanner codec**: without building the message DOM or re-serializing the
  args on every `invoke`.
- **Startup**:
  - `OW_GPU=auto|on|off` (WebKit acceleration policy); `off` disables the DMABUF
    renderer → **−~300 ms and −30 MB** headless (default `auto`).
  - **`T+ms` marks** (`Log::StartupBegin`/`StartupMark`) to profile startup
    without `strace`.
  - **Window visible earlier** before injecting/loading (`PShow` moved): from
    `T+371` to **`T+183 ms`**.
- **SHM fix**: immediate `unlink` after `mmap` (Linux) + sweeping orphan regions.
  A crash left `owear-shm-*` in `XDG_RUNTIME_DIR`; 543 files filled `tmpfs` →
  **SIGBUS** when reading a 5 MB SHM block.

## Distribution — D1 (single binary) — in progress

- **A single binary**: `tools/owear-pack.mjs` packs a bundle (`app/`, `modules/`,
  `manifest.json`) **inside** the kernel → one file (`MiApp`; `.exe` on Windows).
  Format: `[kernel][payload.tar.gz][OWPK1 footer]`.
- **`src/Pack`**: the kernel reads **its own image**, extracts the payload to cache
  (idempotent) and exposes `OW_MODULES_DIR`/`OW_ASSETS_DIR`/`OW_APP_MAIN` to the
  content → the rest of the kernel **is unchanged**. **Node is NOT embedded**: it
  is resolved/installed separately (system detection or download).
- **fix(tar)**: the extractor put the 512 B **padding** inside the file (broke
  `.js`; Node "worked" because it's an ELF with trailing zeros).
- **fix(app://)**: `app://index.html` treated the name as a host and **relative**
  resources 404'd; now `app://host/path` serves `path`.
- **D1 pending**: `installer` module + SDK, custom UI (`owear.installer`),
  `ow package` (Linux/Windows installers, same binary = installer + app),
  `owear.pack.order/protect` (API order/security inside the binary).


---

## D1 — Modular installers (included in 0.1.3)

The installer is **a separate Owear app** that embeds the app payload and a
contract **bridge**; it is built into an installer binary and the installed
result can be a single binary (`minimal`) or an organized tree (`layout`). The
uninstaller is another binary with the same bridge.

## Added

- **`installer` builtin** (`src/api/installer`, `kind=builtin`): native API for
  installer/uninstaller mode.
  - `mode` · `info` · `bridge` · `payloadList` · `payloadRead`
  - `plan` · `install` · `uninstall` · `verify` · `state`
  - `shortcuts` · `launch` · `elevate`
  - Platform integration: **Linux** (`.desktop` in applications/desktop/autostart)
    and **Windows** (`IShellLink` + uninstall registry entry under
    `HKCU\...\Uninstall`).
- **Installer mode in the kernel**: on startup, if the embedded payload contains
  `installer.json` or `uninstaller.json`, the kernel sets `OW_MODE`
  (`installer`/`uninstaller`), serves `ui/` as assets (`OW_ASSETS_DIR`), exposes
  the installer sidecar (`OW_APP_MAIN`) and publishes metadata
  (`OW_APP_VERSION`/`OW_APP_ID`/`OW_APP_NAME`).
- **Bridge (`owear.bridge.ts`)**: a **data-only** contract app ↔ installer ↔
  uninstaller. It is written with `defineBridge()` from `@owear/core` (with
  validation) and `ow build installer` compiles it and **embeds** it as
  `bridge.json` in the installer binary. It defines the app, per-platform
  `targets` (format, layout, preset, dir, shortcuts, scope), `order`, `protect`,
  `node` and `hooks`.
- **SDK `@owear/core`**: a new `installer` API (`mode/info/bridge/payloadList/
  payloadRead/plan/install/uninstall/verify/state/shortcuts/launch/elevate`) and
  `defineBridge` with full types.
- **CLI**:
  - `ow create installer [dir]` and `ow create uninstaller [dir]` (templates).
  - `ow build app [--format binary|deb|appimage]` (app payload).
  - `ow build installer [--mode minimal|layout]` → installer binary.
  - `ow build uninstaller` → uninstaller binary.
- **`tools/owear-installer.mjs`**: assembles the installer payload (`ui/`,
  `payload/`, `bridge.json`, `installer.json`, `modules/`) and packs it inside
  the kernel using the existing `OWPK1` format.
- **Templates**: `packages/cli/template-installer/` (vanilla UI + Node sidecar)
  and its `uninstaller/` sub-template (same bridge, uninstalls).
- **Schemas**: `schemas/owear.bridge.schema.json` and
  `schemas/owear.pack.schema.json`.
- **Linux app formats** (`ow build app --format`):
  - `deb`: a real `.deb` (ar + `control.tar.gz` + `data.tar.gz`) generated in
    Node, with no external dependencies; installs to `/opt/<slug>` + a launcher
    in `/usr/bin` + `.desktop`.
  - `appimage`: an `AppDir` (AppRun + `.desktop` + icon) packed with `appimagetool`
    if available.
- **Windows `.MSI`**: `ow build app --format msi` generates a **WiX v3** source
  from the stage and compiles it with `wixl` (msitools) or `wix`; if there is no
  toolchain, it leaves the `.wxs` ready.
- **`protect`** (integrity + flags) applied by the `installer` builtin: `hidden`
  hides groups from the plan/listing and `readonly` sets read-only permissions on
  install (on top of the manifest integrity hashes).
- **Bridge hooks**: `preInstall`/`postInstall`/`preUninstall`/`postUninstall` are
  emitted as the `installer.hook` event during install and uninstall.

### Payload modes

- **`minimal`** (default): the payload is **a single binary** (kernel + app),
  produced with `owear-pack`; the installer copies it and creates shortcuts.
- **`layout`** (alias `divider`): a **folder tree** (`app/`, `modules/`,
  `manifest.json`, …) placed at the destination, with order/preset via `order`
  and `layout` (`flat`/`tree`).

## Changed

- `src/Core/App/Internal.cpp`: payload startup distinguishes
  **installer / uninstaller / single-binary app**.
- `ow --help`: documents `create installer|uninstaller` and
  `build app|installer|uninstaller`.
- `tools/gen-apis.mjs` regenerated (24 APIs: 17 modules + 7 builtins).

## Fixed

- The `installer` builtin unwraps the dispatcher args (they arrive as an array;
  the first object is used).

## Notes

- Verified end-to-end over the control socket: `mode → plan → install → state →
  verify → uninstall` (it really installs/uninstalls at the destination), and
  `protect`/`hooks` (hides + read-only + `installer.hook` events).
- Pending: optional real payload **encryption** (`protect` is currently integrity
  + flags), and **macOS** (no toolchain in the dev environment).
