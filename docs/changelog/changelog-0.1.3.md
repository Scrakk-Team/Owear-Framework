<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.3

Arreglo de la ventana de Windows (en blanco / "no responde"), toolchain de
compilación cruzada Linux → Windows, **`titleBarOverlay`** (botones nativos de
ventana dentro de la titlebar custom) en Linux, y un **puente Node** para
exponer Node a la UI sin IPC por defecto. Se añade el **Bloque A de
ejecución/IPC**: workers Node con canal, `windowId` en los handlers, envío
dirigido `webContents.send` y `MessageChannel`/`MessagePort`. Se suma el
**Bloque B**: `protocol` (esquemas personalizados), `safeStorage` (DPAPI /
AES-GCM), `theme` (nativeTheme) y permisos de `session`; y el sidecar Node ya
muere con el kernel.

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
    (system|light|dark)/`watch` + evento `theme.changed`. **Linux: GSettings
    `color-scheme` + tema GTK**; **Windows: registro `AppsUseLightTheme`** (watch
    por sondeo). SDK: `ow.theme`. Verificado en Linux.
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
