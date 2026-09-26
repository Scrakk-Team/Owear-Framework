<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.3

Arreglo de la ventana de Windows (en blanco / "no responde"), toolchain de
compilación cruzada Linux → Windows, **`titleBarOverlay`** (botones nativos de
ventana dentro de la titlebar custom) en Linux, y un **puente Node** para
exponer Node a la UI sin IPC por defecto.

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
- **`titleBarOverlay` — botones nativos dentro de la titlebar custom (Linux)**:
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
- `examples/starter`: usa `titleBarOverlay` y oculta sus propios botones,
  reservando el hueco con `--ow-overlay-width/height`.
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
