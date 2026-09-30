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

- **Cross-compilación Linux → Windows**: `clang-cl` + `lld-link` con el
  MSVC/Windows SDK descargado por `xwin` (`cmake/windows-cross.cmake`). Genera
  `owear.exe` + módulos desde Linux, sin Visual Studio ni CI. El `.exe` sale con
  **CRT estático** (`/MT`) y deps estáticas → **sin VCRUNTIME ni DLLs extra**.
- Opción `OW_WITH_OPENSSL` (default ON): con `OFF` el kernel usa un HTTP stub
  sin TLS (`Runtime/Http_nossl.cpp`) y omite los módulos `net`/`updater`. Pensada
  para el cross-build de desarrollo.
- `tools/windows-cross/build-deps.sh` (cross-build de zlib), `build.sh`,
  `serve.sh` y `tools/windows/dev-pull.ps1` (transferencia a Windows por HTTP).
- **`titleBarOverlay` — botones nativos dentro de la titlebar custom (Linux + Windows)**:
  - API: `titleBarOverlay: true | { height, color, symbolColor, buttonColor }` en
    `BrowserWindowOptions` y `win.setTitleBarOverlay(...)`; comando de control
    `window.setTitleBarOverlay`. El hueco reservado se expone al renderer como
    `window.__owTitlebarOverlay = { enabled, height, width }`.
  - En Linux son los botones **del tema** (clase `titlebutton`, iconos simbólicos
    freedesktop): tamaño, estilo, hover y espaciado los define la distro (nada
    hardcodeado). Se superponen con un `GtkOverlay`.
  - Color del glifo (`symbolColor`), fondo de la banda (`color`) y **fondo
    interno del círculo** (`buttonColor`) configurables; por defecto, el tema.
  - **Sombra de ventana del tema** vía CSD (titlebar vacío de 0px) y **esquinas
    redondeadas** del contenido web (`clip-path` + `border-radius`) con el **radio
    leído del tema** (`decoration { border-radius: N }`).
  - **Resize en Wayland**: zonas `GtkEventBox` en bordes/esquinas (el filtro GDK
    es solo X11, en Wayland no dispara).
  - **Windows (WebView2): YA FUNCIONA.** Los botones min/max/close se **dibujan a
    mano con GDI+**, idénticos a Electron (`windows_icon_painter.cc`): icono
    **10px**, min/max/restore **sin anti-alias** y rect insetado 0.5, restaurar =
    **dos cuadrados de 8px desplazados 2**, cerrar = **X con AA**. Se dibujan en
    un popup **top-level layered** (`UpdateLayeredWindow`) con fondo **alpha=1**
    (imperceptible) para que el **hit-test** cubra todo el rect → hover/press
    exactos. Fondo transparente real (se ve la titlebar).
- **Puente Node (`node`)** — el renderer puede **usar Node sin IPC por defecto**:
  - `ow.invoke('node', 'call', { fn, args })` ejecuta un handler del proceso
    principal registrado con `app.handle(fn, handler)`; el main responde y puede
    empujar eventos con `app.send(name, payload?, windowId?)` → `ow.on(name)`.
  - Resolución **asíncrona** (el kernel reenvía `node.request` al main por el
    control socket; el main contesta con `node.respond`). Modularizado con el
    manifiesto `api/node/owear.module.json` (builtin `node`, función `call`).
  - **Opt-in**: los caminos calientes (fs, terminal/PTY…) siguen yendo directo
    renderer → kernel → módulo nativo, sin Node en medio. Pensado para exponer
    Node a la UI (p. ej. el extension host de VS Code).
- **Bloque A — ejecución e IPC** (workers, contexto, webContents, MessagePort):
  - **Workers Node con canal (`app.forkWorker`)** — reemplazo de
    `utilityProcess.fork`: el main (que ya es Node real) lanza un hijo con canal
    IPC (`child_process.fork`) y un **shim de `process.parentPort`** para que los
    workers portados desde Electron funcionen **sin cambios**. CLI:
    `app/workers/**` se compilan a `workers/` y se exponen en `OW_APP_WORKERS`
    (`ow dev`/`ow build`); `app.workersDir()`. API: `postMessage/on(message|exit)/kill/pid`.
  - **`windowId` en los handlers Node** — el kernel propaga la ventana de origen
    al main y el SDK la expone con `app.handleContext(fn, (ctx, ...args) => …)`
    (`ctx.windowId`). `app.handle` sigue igual (aditivo).
  - **`webContents.send` dirigido** — `win.webContents.send(name, payload)` en el
    SDK (paridad Electron) envía ONLY a esa ventana; el renderer lo recibe con
    `ow.on(name, cb)`.
  - **`MessageChannel`/`MessagePort` (`app.createChannel`)** — canal bidireccional
    main ↔ renderer sin registrar handlers por llamada: `app.sendPort(windowId,
    name, port)` transfiere un extremo; en el renderer `ow.port(id)` (bridge)
    recibe con `ow.on(name, ({ port }) => …)`. Enrutado por el bridge; el
    transporte binario sin copia kernel→renderer sigue siendo `ow-shm://`.
  - **Módulos nativos N-API**: documentados en `docs/NATIVE.md` (runtime Node
    real → addons N-API y prebuilds de Node cargan tal cual; los binarios de
    Electron hay que reconstruirlos para Node).
  - **Tests**: `@owear/core` estrena `node --test` (`packages/core/test`:
    `forkWorker` round-trip/exit y `createChannel` main↔main). Verificado además
    E2E en Linux (Xvfb): worker, `windowId`, `webContents.send` y puerto
    renderer↔main.
- **Bloque B — protocolo / datos / OS** (`protocol`, `safestorage`, `theme`, `session`):
  - **`protocol` (esquemas personalizados)** — el main registra un esquema y el
    kernel lo sirve con **handler en el main** o **directorio**:
    `app.protocol(name, { privileged, serve, handler })`. Handler puede devolver
    `Response`, `{status, headers, body}` o `string`. Linux: registro dinámico en
    WebKitGTK con `finish` asíncrono; Windows: `WebResourceRequested` + deferral.
    Verificado en Linux (handler y `serve`).
  - **`safeStorage`** (módulo `api/safestorage`) — `isAvailable`, `encrypt` →
    `{data, encrypted}`, `decrypt` (acepta texto plano). **Windows: DPAPI**;
    **Linux: AES-256-GCM** con clave local 0600 en el data dir de la app. SDK:
    `ow.safeStorage`. Verificado en Linux (round-trip + persistencia de clave).
  - **`theme` (nativeTheme)** (módulo `api/theme`) — `get`/`isDark`/`setSource`
    (system|light|dark)/`watch`/`unwatch` + evento `theme.changed`. **Linux:
    GSettings `color-scheme` + tema GTK**; **Windows: registro
    `AppsUseLightTheme`** (watch por sondeo). **Forzar el contenido**:
    `theme.setSource` → kernel `window.setColorScheme` → **Windows WebView2
    `PreferredColorScheme`** (cambia el `prefers-color-scheme` real); en **Linux
    WebKitGTK no expone forzarlo** (la app reacciona a `theme.changed`). SDK:
    `ow.theme` (+ alias `ow.nativeTheme`). Verificado en Linux (lectura/eventos);
    Windows compila.
  - **`session` — permisos del WebView** — `session.onPermissionRequest(handler)`
    (deniega si no hay handler). `PermissionBroker` en el kernel + comandos
    `session.setPermissionHandler`/`session.respondPermission`. **Linux: señal
    `permission-request` de WebKitGTK**; **Windows: `add_PermissionRequested`
    con deferral**. Verificado en Linux (geolocation).
  - **`session` — particiones (perfiles)** — `session.fromPartition(name)` +
    `BrowserWindow({ session })` aíslan cookies/localStorage/IndexedDB/cache por
    perfil. Linux: data dir por partición (`…/webkit/<partición>/{data,cache}`);
    Windows: perfil por partición (`…\owear\WebView2\<partición>`). Verificado en
    Linux: `persist:a` y `persist:b` no comparten `localStorage`; dos ventanas de
    la misma partición sí.
  - **`session` — `webRequest`** (`onBeforeRequest`: cancelar/redirigir) —
    `webRequest.onBeforeRequest({ urls }, handler)`. Windows: **todos los
    requests** (`WebResourceRequested` + deferral). Linux: **navegaciones**
    (`decide-policy`); WebKitGTK 2.52 ya **no expone `send-request`**, así que
    los subrecursos no se pueden interceptar en Linux (documentado). Verificado
    en Linux (bloqueo de navegación).
  - Pendiente de la sesión (roadmap): `webRequest.onHeadersReceived` y (Linux)
    subrecursos — bloqueado por la API de WebKitGTK.
- **Ciclo de vida del sidecar** — el proceso Node **muere con el kernel**:
  Linux/macOS `PR_SET_PDEATHSIG(SIGTERM)` + `NodeManager::ShutdownSidecar()`
  (SIGTERM y, si no sale, SIGKILL); Windows Job Object `KILL_ON_JOB_CLOSE` +
  `TerminateProcess`. Verificado en Linux: tras `app.quit()` el PID del sidecar
  desaparece.
- **`docs/extra/probar-en-starter.md`** — guía viva (WIP) de cómo probar cada
  sistema en el starter (Linux + Windows), que se irá rellenando.
- **Bloque C — shell de app** (empezado por C1):
  - **`app` completo** — `getPath`/`setPath` (home, appData, userData, temp,
    cache, logs, downloads, documents, desktop, pictures, music, videos, exe,
    appPath), `getName`/`setName`, `getVersion` (de `package.json`),
    `isPackaged`, `getAppPath`, **`commandLine`** (`appendSwitch`/`appendArgument`/
    `getSwitchValue`/`hasSwitch`, aplicado al crear el WebView: Windows
    `AdditionalBrowserArguments`; Linux best-effort) y **eventos**
    (`window-all-closed`, `before-quit`, `will-quit`, `second-instance`,
    `child-process-gone`). Kernel: `app.info` extendido (`name/appPath/exePath/
    packaged`), `app.setName`, `app.commandLine.*`. SDK: `app` es EventEmitter.
    Verificado en Linux (rutas, identidad, `commandLine` y los 3 eventos);
    Windows compila.
  - **`dialog` completo (C2)** — `showOpenDialog` (`properties`:
    `openFile`/`openDirectory`/`multiSelections`/`showHiddenFiles`, `filters`,
    `defaultPath`, `buttonLabel` → `{ canceled, filePaths }`), `showSaveDialog`
    (`filters`, `defaultPath` → `{ canceled, filePath }`) y `showMessageBox`
    (`type`, `message`/`detail`, `buttons`, `defaultId`, `cancelId`,
    `checkboxLabel` → `{ response, checkboxChecked }`). Linux: GtkFileChooser +
    GtkMessageDialog (con checkbox); Windows: `IFileOpenDialog`/`IFileSaveDialog`
    + `TaskDialogIndirect`. SDK: `ow.dialog`. Registrado en Linux (5 funciones);
    Windows compila. (`open`/`messageBox` se mantienen.)
  - **`webContents` (C3)** — `win.webContents` es un objeto estilo Electron:
    `send`, `capturePage()` (PNG → `{toPNG(), toDataURL()}`; el kernel devuelve
    base64 con `{base64:true}`), `loadURL`, `reload`, `openDevTools`, `getURL`,
    `getTitle`, `executeJavaScript`, y **`setWindowOpenHandler`** (allow/deny de
    `window.open`). **Eventos** con nombres Electron: `did-finish-load`,
    `did-fail-load`, `did-start-navigation`, `did-navigate`, `page-title-updated`,
    **`before-input-event`** (teclado: WebKitGTK `key-press/release-event`;
    WebView2 `AcceleratorKeyPressed`). Kernel: event sink en el backend + broker
    de ventanas emergentes (`WindowOpenBroker`) + `capturePage` `{base64}`.
    Verificado en Linux (`did-finish-load`, `capturePage`, `getURL`); Windows
    compila.
  - **`nativeImage` (C4)** — sin dependencias nativas: **códec PNG** en el SDK
    (Node `zlib`) que soporta colorType 0/2/3/4/6 y bitDepth 1/2/4/8/16.
    `nativeImage.createFromPath`/`createFromBuffer`/`createFromDataURL` (síncronos
    como Electron) + `getSize`, `isEmpty`, `toPNG`, `toDataURL`, `resize`
    (bilineal), `crop`. JPEG: `getSize` (SOF) y `toJPEG` (si la fuente ya es
    JPEG). `webContents.capturePage()` ahora devuelve un `NativeImage`.
    Verificado (tests SDK).
  - **`Menu`/`MenuItem` (C5)** — API pulida estilo Electron en el **main**:
    `Menu.buildFromTemplate([...])`, `setApplicationMenu`/`getApplicationMenu`,
    `menu.popup({ window, x, y })`. `MenuItem`: `id`, `label`, `role` (quit,
    minimize, close, reload, toggleDevTools, undo/cut/copy/paste/selectAll…),
    `type` (normal/separator/submenu/**checkbox**/**radio**), `checked`, `enabled`,
    `visible`, `accelerator` (display), `submenu`, `click(item, window)`. Los
    **clicks van al main** (handlers `click` + roles). Kernel: template común
    (`ow/Menu.hpp`) con tipos/estados; Linux GTK (`GtkCheck/RadioMenuItem`) y
    Windows `HMENU` (popup `TrackPopupMenu` + **menubar** vía
    `window.setApplicationMenu` + `WM_COMMAND`). Tests SDK; Windows compila.
  - **`Tray` (C6)** — icono de bandeja completo (API intermedia Tauri/Electron):
    `new Tray(image?)`, `setImage`, `setPressedImage`, `setToolTip`, `setTitle`,
    `setContextMenu(Menu)`, `popupContextMenu`, `destroy`, y eventos
    `click`/`right-click`/`double-click`. Icono = `NativeImage` o ruta → PNG
    base64 al kernel. **Linux: SNI nativo (GDBus)** — `org.kde.StatusNotifierItem`
    + `com.canonical.dbusmenu` implementados a mano (sin librerías), registrados
    con el `StatusNotifierWatcher` (GNOME/Zorin con la extensión, KDE, XFCE+plugin)
    → `docs/extra/GNOME.md`.
    **Windows: `Shell_NotifyIcon`** con
    menú contextual (`TrackPopupMenu`), eventos y **PNG→HICON** vía GDI+. El menú
    reutiliza el template de C5. Verificado en Linux (registro + ciclo);
    Windows compila.
  - **`print` / `printToPDF` (C8)** — `win.webContents.print()` (diálogo del
    sistema) y `win.webContents.printToPDF()` (→ `Buffer` PDF vía comando
    asíncrono `window.printToPDF`). **Windows: WebView2 `PrintToPdf` +
    `ShowPrintUI`** (vectorial). **Linux: `print`** con `WebKitPrintOperation`
    (diálogo) y **`printToPDF`** vía **snapshot de página completa → Cairo PDF**
    (rasterizado; WebKitGTK no expone API de PDF, y el backend "Print to File" de
    GTK bloquea). Verificado en Linux (`%PDF`, ~2.4 KB); Windows compila.
  - **`BrowserWindow` completo (C9)** — opciones: `parent`, `modal`,
    `transparent`, `backgroundColor`, `movable`, `minimizable`, `maximizable`,
    `closable`, `fullscreenable`, `skipTaskbar`, `alwaysOnTop`, `hasShadow`,
    `min/maxWidth/Height`, `aspectRatio`, `show`. Métodos: getters (`isVisible`,
    `isFocused`, `isResizable`, `isMovable`, `isMinimizable`, `isMaximizable`,
    `isClosable`, `isAlwaysOnTop`, `isKiosk`, `isDestroyed`, `isFullScreen`),
    setters (`setResizable/Movable/Minimizable/Maximizable/Closable`,
    `setAlwaysOnTop(flag,level)`, `setSkipTaskbar`, `setHasShadow`, `setKiosk`,
    `setIgnoreMouseEvents`, `setProgressBar`, `setBackgroundColor`, `moveTop`,
    `setAspectRatio`), geometría (`getContentBounds/Size`, `setContentSize`,
    `get/setMinimumSize`, `get/setMaximumSize`) y **estáticos** `getAllWindows`,
    `getFocusedWindow`, `fromId`. **Eventos** con nombres Electron (dash):
    `enter-full-screen`, `leave-full-screen`, `always-on-top-changed`,
    `page-title-updated`, `show`, `hide`, `restore`, `minimize`, `resized`,
    `moved` (+ los camelCase previos). Linux (GTK) y Windows (Win32: estilos,
    `SetWindowPos`, `ITaskbarList3`, `WS_EX_TRANSPARENT`, `WM_SIZING`/`WM_GETMINMAXINFO`).
    Verificado en Linux; Windows compila.
- **Webviews embebidas (`webview`, Linux)** — cada ventana puede tener N WebViews
  hijas, **cada una con su propio proceso**, embebidas y controlables por API:
  - Builtin `webview` (`api/webview/owear.module.json`, Linux + Windows): `create, destroy,
    setBounds, load, back, forward, reload, stop, canBack, canForward, getURL,
    getTitle, eval, setVisible, setZoom, devtools, findInPage, findStop`.
  - Renderer: `ow.invoke('webview', 'create', { url, x, y, width, height })` +
    eventos `ow.on('webview.loadChanged|urlChanged|titleChanged|loadFailed')`.
  - Linux (WebKitGTK): cada hija en su propio contenedor overlay (solo
    intercepta su rectángulo) + contexto WebKit compartido con data dir por app
    y **un WebProcess por vista**; base para Windows/macOS.
  - Windows (WebView2): cada hija en su **HWND hijo propio** + controller de
    WebView2 parentado ahí (z-order/bounds fiables); environment con user data
    dir propio. `titleBarOverlay` en Windows **dibuja los botones estilo
    Win10/11** (GDI+, ventana layered: solo glifos/hover sobre la titlebar) y
    expone `__owTitlebarOverlay` al renderer.
  - **Foco automático**: las hijas **no roban el foco al cargar** (arrancan con
    `can_focus=FALSE`); el click sobre una hija se lo da y el click fuera de las
    hijas vuelve a la principal.
- `examples/starter`: **rediseño en curso** (WIP) — fuentes, layout con
  containers y uso de las APIs nuevas (`titleBarOverlay`, navegador embebido).
- **Bloque C — C10 `screen`/`Display` + `powerMonitor` (multi-monitor y energía)**:
  - **`screen` (módulo, Linux + Windows)**: `getAllDisplays`, `getPrimaryDisplay`,
    `getCursorScreenPoint` con **Display completo** (`id`, `bounds`, `size`,
    `workArea`, `workAreaSize`, `scaleFactor`, `rotation`, `internal`, `label`,
    `displayFrequency`, `colorDepth`, `depthPerComponent`, `colorSpace`,
    `monochrome`, `touchSupport`, `accelerometerSupport`, `detected`,
    `nativeOrigin`), **id estable** (Windows: hash del `szDevice`; Linux: por
    `GdkMonitor*`) y **eventos** `screen.added` / `screen.removed` /
    `screen.changed` con `watch`/`unwatch`. Windows: ventana oculta top-level en
    un hilo propio para `WM_DISPLAYCHANGE`; Linux: señales de `GdkDisplay` y
    `notify::geometry|workarea|scale-factor`.
  - **`power` (módulo, Linux + Windows)**: `monitorStart`/`monitorStop` y eventos
    `power.suspend`/`resume`/`shutdown`/`lock`/`unlock`/`ac`/`battery`; `idleTime`,
    `idleState` y `isOnBattery`; inhibidores `inhibitStart`/`inhibitStop`.
    Linux: logind (`PrepareForSleep`/`PrepareForShutdown`/`Session Lock/Unlock`) +
    UPower (con fallback `/sys`) + X11 Xss (`dlopen`, Wayland → `unknown`).
    Windows: ventana oculta con `WM_POWERBROADCAST`/`WM_ENDSESSION` +
    `WTSRegisterSessionNotification` (lock/unlock) + `GetLastInputInfo` +
    `GetSystemPowerStatus`.
  - **SDK**: `screen` (EventEmitter: `added`/`removed`/`changed`, `getAllDisplays`,
    `getPrimaryDisplay`, `getCursorScreenPoint`, `getDisplayNearestPoint`,
    `getDisplayMatching`, `screenToDipPoint`/`dipToScreenPoint` + alias),
    `powerMonitor` (EventEmitter: `suspend`/`resume`/`shutdown`/`lock`/`unlock`/
    `ac`/`battery`, `getIdleTime`, `getIdleState`, `isOnBatteryPower`,
    `onBatteryPower`) y `powerSaveBlocker` (`start`/`stop`/`isStarted`). Watch
    perezoso al primer uso. Los eventos de módulo se re-emiten por nombre en el
    canal (`app.__channel.on('screen.added', …)`).
- `examples/starter`: **rediseño en curso** (WIP) — fuentes, layout con
  containers y uso de las APIs nuevas (`titleBarOverlay`, navegador embebido).
- Dev: `OW_TITLEBAR_OVERLAY[=_HEIGHT]` para probar el overlay con `OW_DEMO=1`.
- Marcador de build en el log del kernel (`OWEAR KERNEL BUILD ...`) y
  `PCreate: estilo=... custom=...` para diagnóstico.

## Fixed

- **Ventana en blanco / "no responde" en Windows**: el controller de WebView2
  **no se redimensionaba** en `WM_SIZE` (quedaba con bounds inválidos). Ahora se
  llama a `Resize` en cada `WM_SIZE`.
- `WM_NCCALCSIZE` con titlebar custom ahora aplica los **insets del marco**
  (técnica Electron) en vez de devolver `0` a secas, que rompía el hit-testing
  del borde en Windows 10.
- **DPI awareness** (`PER_MONITOR_AWARE_V2` antes de crear ventanas).
- **Módulos cargados dos veces**: `ModuleLoader::SearchPaths` deduplica
  `OW_MODULES_DIR` vs `<exe>/modules`.
- Diagnóstico del ciclo WebView2: `environment → controller → navigate`
  (URL, bounds, `nav completed`, `ProcessFailed`) y `put_IsVisible(TRUE)`.
- **Windows 10: el caption nativo seguía apareciendo** con titlebar custom. Ahora
  se mantiene `WS_OVERLAPPEDWINDOW` (para no perder esquinas/sombra en Win11) y el
  caption se quita con `WM_NCCALCSIZE` reaplicado con `SWP_FRAMECHANGED` (el
  primer `WM_NCCALCSIZE` corre dentro de `CreateWindowEx`, antes de tener el
  estado de la ventana).
- **Linux: hueco fantasma** encima del contenido (el tema aplica su `min-height`
  a la clase `.titlebar` del CSD vacío) → se anula con CSS en el contexto del
  propio titlebar.
- **Linux: el redondeo del contenido no se veía** → el fondo del `body` se
  propaga al canvas y se pinta cuadrado; ahora se clipea la raíz con
  `clip-path: inset(0 round Npx)` (N = radio del tema).
- **Linux: los colores del overlay no aplicaban** → en GTK3 un style provider en
  un widget solo afecta a ese widget (no a sus hijos); se registra a nivel de
  screen con selectores por `id`.
- **Linux: almacenamiento del WebView aislado por app** → el backend WebKitGTK
  usaba el `WebsiteDataManager` por defecto (compartido entre apps), así que
  `localStorage`/`IndexedDB`/cache podían cruzarse. Ahora usa un data manager con
  base dir **por app** (`$XDG_DATA_HOME/owear/<app-id>/webkit/{data,cache}`) y
  registra `app://` como **esquema seguro + CORS** (origin estable).
- **Cierre lento en Linux y Windows**: el flujo de veto (`closeRequested`) esperaba
  siempre `OW_CLOSE_TIMEOUT_MS` (~1s) aunque la app no escuchara. Ahora, si el
  renderer **no tiene listener** de `closeRequested`, responde `allow` **al
  instante** → la ventana deja de aparecer de inmediato. Si hay listener, se
  mantiene el veto con su timeout.
- **Windows: `dialog.dll` no cargaba** (`STATUS_ENTRYPOINT_NOT_FOUND` / err 182):
  importaba `TaskDialogIndirect` (comctl32) estáticamente y sin manifiesto v6 el
  DLL entero fallaba. Ahora se resuelve **dinámicamente** (`GetProcAddress`) y cae
  a `MessageBox` si no está.
- **Windows: módulos viejos / cargados dos veces** → `ModuleLoader` ahora prefiere
  `<exe>/modules` (mismo build que el kernel) y **deduplica por nombre**, así un
  runtime package antiguo en `OW_MODULES_DIR` ya no gana (adiós "función
  desconocida").
- **Dev en el monorepo: módulos desactualizados** → el CLI (`stockModulesPath`)
  prefiere el **build local** al runtime package prebuilt (artefacto git-ignored
  que puede estar viejo).
- **Windows: `capturePage` crasheaba** (`ACCESS_VIOLATION`): el `capturePage`
  síncrono hacía un *pump* de mensajes anidado que reentraba. Ahora la captura es
  **asíncrona** por comando de control (`window.capturePage`), sin bucle anidado.
- **Linux: `menu.popup`** daba `Gtk-CRITICAL: no trigger event` (el popup llega
  desde un click async, sin evento GDK): ahora se coloca con
  `gtk_menu_popup_at_rect` sobre el root window con un **evento de trigger
  sintético**.
- **Sidecar Node huérfano**: el proceso Node del main ahora **muere con el kernel**
  (`PR_SET_PDEATHSIG` en Linux/macOS + `ShutdownSidecar`; Job Object
  `KILL_ON_JOB_CLOSE` en Windows), con margen para entregar `before-quit`/
  `will-quit` antes de terminarlo.
- **Windows: cuelgue/crash al cambiar de tema repetidamente** → **data race** en
  el módulo `theme` (`g_source`, un `std::string`, lo escribía el main y lo leía el
  hilo de `watch`) → ahora con **mutex**. Y `window.setColorScheme` estaba **tras**
  el bloque que exige `windowId` (que el SDK no pasa) → **nunca aplicaba** el
  `PreferredColorScheme`; movido → ahora sí cambia el `prefers-color-scheme` real.
- **`BrowserWindow.getAllWindows`/`getFocusedWindow` locales en el SDK** →
  **síncronos** y resueltos desde el registro del SDK (foco por eventos
  `focus`/`blur`), como Electron. Antes usaban comandos nuevos del kernel que en
  **Windows colgaban la ventana** (el botón "Ventanas" del starter).
- **Windows: cuelgue con clicks rápidos (carga alta)** — dos causas:
  1. El pipe de control llamaba `CancelIoEx(pipe, nullptr)` al encolar cada
     respuesta, lo que cancela **toda** la I/O del handle, **incluidas las
     escrituras en vuelo** del hilo lector → respuestas truncadas que
     **desincronizan el protocolo** del SDK (un `invoke` que nunca resuelve =
     ventana colgada). Además, si el lector no estaba aún bloqueado en
     `ReadFile`, la respuesta quedaba varada. Ahora el lector **sondea** con
     `PeekNamedPipe` (drena el outbox en ~1 ms) y solo escribe cuando no hay
     lectura pendiente.
  2. El bucle principal encolaba **un `kWmOwPump` por callback** → una ráfaga de
     N callbacks generaba N pasadas de `GetMessage` (N-1 vacías). Ahora hay **un
     único pump en vuelo** (`atomic` + rearme tras drenar).

## Optimización (benchmarks Owear vs Electron vs Tauri)

Medido con el harness propio (`benchmarks/`). Cambios de rendimiento:

- **Puente sin `eval` — RPC por esquema `ow-rpc://`**: el renderer llama a los
  módulos nativos (`.owm`) con `fetch` (args en el body POST, respuesta en el
  body) → elimina el `postMessage` de request **y** el `executeJavaScript` de
  respuesta (el motor ya no compila código por llamada). Linux y Windows
  (WebView2). IPC secuencial ~2×, concurrente ~4-5×, payload 5 MB ~1.5×.
- **Payload grande por SHM** (`_applyShm` + `ow-shm://`) en la ruta postMessage.
- **Códec con scanner de spans**: sin construir el DOM del mensaje ni
  re-serializar los args en cada `invoke`.
- **Arranque**:
  - `OW_GPU=auto|on|off` (política de aceleración de WebKit); `off` desactiva el
    renderer DMABUF → **−~300 ms y −30 MB** en headless (default `auto`).
  - **Marcas `T+ms`** (`Log::StartupBegin`/`StartupMark`) para perfilar el
    arranque sin `strace`.
  - **Ventana visible antes** de inyectar/cargar (`PShow` movido): de `T+371` a
    **`T+183 ms`**.
- **fix SHM**: `unlink` inmediato tras `mmap` (Linux) + barrido de regiones
  huérfanas. Un crash dejaba `owear-shm-*` en `XDG_RUNTIME_DIR`; 543 ficheros
  llenaban `tmpfs` → **SIGBUS** al leer SHM de 5 MB.

## Distribución — D1 (single binary) — en curso

- **Un solo binario**: `tools/owear-pack.mjs` empaqueta un bundle (`app/`,
  `modules/`, `manifest.json`) **dentro** del kernel → un único fichero
  (`MiApp`; `.exe` en Windows). Formato: `[kernel][payload.tar.gz][footer OWPK1]`.
- **`src/Pack`**: el kernel lee **su propia imagen**, extrae el payload a caché
  (idempotente) y expone `OW_MODULES_DIR`/`OW_ASSETS_DIR`/`OW_APP_MAIN` al
  contenido → el resto del kernel **no cambia**. **Node NO se embebe**: se
  resuelve/instala aparte (detección de sistema o descarga).
- **fix(tar)**: el extractor metía el **padding** de 512 B dentro del fichero
  (rompía `.js`; Node “funcionaba” por ser un ELF con ceros al final).
- **fix(app://)**: `app://index.html` trataba el nombre como host y los recursos
  **relativos** daban 404; ahora `app://host/path` sirve `path`.
- **Pendiente D1**: módulo `installer` + SDK, UI custom (`owear.installer`),
  `ow package` (instalables Linux/Windows, mismo binario = instalador + app),
  `owear.pack.order/protect` (orden/seguridad de las APIs dentro del binario).


---

## D1 — Instaladores modulares (incluido en 0.1.3)

El instalador es **una app Owear aparte** que lleva embebido el payload de la
app y un **bridge** de contrato; se compila a un binario instalador y el
resultado instalado puede ser un solo binario (`minimal`) o un árbol
organizado (`layout`). El desinstalador es otro binario con el mismo bridge.

## Added

- **Builtin `installer`** (`src/api/installer`, `kind=builtin`): API nativa del
  modo instalador/desinstalador.
  - `mode` · `info` · `bridge` · `payloadList` · `payloadRead`
  - `plan` · `install` · `uninstall` · `verify` · `state`
  - `shortcuts` · `launch` · `elevate`
  - Integración de plataforma: **Linux** (`.desktop` en aplicaciones/escritorio/
    autostart) y **Windows** (`IShellLink` + registro de desinstalación en
    `HKCU\...\Uninstall`).
- **Modo instalador en el kernel**: al arrancar, si el payload embebido contiene
  `installer.json` o `uninstaller.json`, el kernel fija `OW_MODE`
  (`installer`/`uninstaller`), sirve `ui/` como assets (`OW_ASSETS_DIR`), expone
  el sidecar del instalador (`OW_APP_MAIN`) y publica metadatos
  (`OW_APP_VERSION`/`OW_APP_ID`/`OW_APP_NAME`).
- **Bridge (`owear.bridge.ts`)**: contrato **data-only** app ↔ installer ↔
  uninstaller. Se escribe con `defineBridge()` de `@owear/core` (con validación)
  y `ow build installer` lo compila y lo **embebe** como `bridge.json` en el
  binario instalador. Define app, `targets` por plataforma (formato, layout,
  preset, dir, shortcuts, scope), `order`, `protect`, `node` y `hooks`.
- **SDK `@owear/core`**: nueva API `installer` (`mode/info/bridge/payloadList/
  payloadRead/plan/install/uninstall/verify/state/shortcuts/launch/elevate`) y
  `defineBridge` con tipos completos.
- **CLI**:
  - `ow create installer [dir]` y `ow create uninstaller [dir]` (templates).
  - `ow build app [--format binary|deb|appimage]` (payload de la app).
  - `ow build installer [--mode minimal|layout]` → binario instalador.
  - `ow build uninstaller` → binario desinstalador.
- **`tools/owear-installer.mjs`**: ensambla el payload del instalador
  (`ui/`, `payload/`, `bridge.json`, `installer.json`, `modules/`) y lo empaqueta
  dentro del kernel con el formato `OWPK1` existente.
- **Templates**: `packages/cli/template-installer/` (UI vanilla + sidecar Node) y
  su sub-template `uninstaller/` (mismo bridge, desinstala).
- **Schemas**: `schemas/owear.bridge.schema.json` y
  `schemas/owear.pack.schema.json`.
- **Formatos Linux de la app** (`ow build app --format`):
  - `deb`: `.deb` real (ar + `control.tar.gz` + `data.tar.gz`) generado en Node,
    sin dependencias externas; instala en `/opt/<slug>` + launcher en
    `/usr/bin` + `.desktop`.
  - `appimage`: `AppDir` (AppRun + `.desktop` + icono) y empaquetado con
    `appimagetool` si está disponible.
- **`.MSI` de Windows**: `ow build app --format msi` genera un fuente **WiX v3**
  desde el stage y lo compila con `wixl` (msitools) o `wix`; si no hay
  toolchain, deja el `.wxs` listo.
- **`protect`** (integridad + flags) aplicado por el builtin `installer`:
  `hidden` oculta grupos del plan/listado y `readonly` fija permisos de solo
  lectura al instalar (además de los hashes de integridad del manifiesto).
- **Hooks del bridge**: `preInstall`/`postInstall`/`preUninstall`/
  `postUninstall` se emiten como evento `installer.hook` durante la instalación
  y desinstalación.

### Modos de payload

- **`minimal`** (default): el payload es **un binario único** (kernel + app),
  producido con `owear-pack`; el instalador lo copia y crea accesos directos.
- **`layout`** (alias `divider`): **árbol de carpetas** (`app/`, `modules/`,
  `manifest.json`, …) colocado en el destino, con orden/preset por `order` y
  `layout` (`flat`/`tree`).

## Changed

- `src/Core/App/Internal.cpp`: el arranque con payload distingue
  **instalador / desinstalador / app single-binary**.
- `ow --help`: documenta `create installer|uninstaller` y
  `build app|installer|uninstaller`.
- `tools/gen-apis.mjs` regenerado (24 APIs: 17 módulos + 7 builtins).

## Fixed

- El builtin `installer` desenvuelve los argumentos del dispatcher (llegan como
  array; se usa el primer objeto).

## Notas

- Verificado end-to-end por el control socket: `mode → plan → install → state →
  verify → uninstall` (instala/desinstala de verdad en el destino), y
  `protect`/`hooks` (oculta + solo-lectura + eventos `installer.hook`).
- Pendiente: **cifrado real** opcional del payload (hoy `protect` es integridad
  + flags), y **macOS** (sin toolchain en el entorno de desarrollo).
