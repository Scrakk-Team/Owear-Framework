<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.4 (WIP / sin publicar)

Mejoras del **sistema de instaladores (D1)** y de su **template por defecto**, más
el port de **draw.io** como ejemplo con instalador propio. Todo en local; **no se
publica en npm** todavía.

## Added

- **Registro de apps instaladas** (capacidad de Owear, en el builtin `installer`):
  `install()` escribe `~/.config/owear/installed/<appId>.json` (Linux) /
  `%APPDATA%\\owear\\installed` (Windows) con `{appId, appName, version, dir,
  mode, installedAt}`; `uninstall()` lo borra. API asociada:
  - `installer.info()` incluye `installed` y `dir` (merge de `installer.json` +
    registro).
  - `installer.state({ dir? })` — **sin `dir`** usa el registrado.
  - `installer.uninstall({ dir? })` — **sin `dir`** usa el registrado.
  - **`installer.list()`** — todas las apps instaladas.
- **Uninstaller del template**: detecta la instalación **por el registro** (sin
  escribir la ruta) y usa `installer.chooseDir()` nativo (fuera `dialog`).
- **API de icono de app**: `BrowserWindow({ icon })` (ruta a PNG/JPEG) y
  `app.setIcon(path)` (icono por defecto de la app, aplicado a las ventanas que
  se creen). El instalador usa además `owear.bridge.ts → app.icon` para el
  `.desktop` (`Icon=`) y **copia el icono a `hicolor`**.
- **`installer.defaultDir()`** y **`installer.chooseDir()`** (nativos en el
  builtin): el UI del instalador ya **no usa `dialog`** (un módulo `.owm`) →
  el instalador es **autocontenido** (solo builtins). `chooseDir` abre el
  selector de carpeta nativo (GTK en Linux, `IFileOpenDialog` en Windows).
- **Template del instalador con los estilos del Starter**: el instalador por
  defecto (`packages/cli/template-installer/`) usa ahora los tokens y
  componentes del app Starter de Owear — **One Dark Pro** + acento rosado,
  **JetBrains Mono** y **Dancing Script** (self-hosted, con `@font-face`),
  titlebar con `brand__mark`, `.btn` con hover interno, `.card`/`.board`/
  `.console`/`.badge`. Se aplica también al sub-template del **desinstalador**.
- **Dev-loop del instalador** (`examples/drawio/dev-installer.mjs`): arranca
  `vite` (dev server del UI del instalador) y lanza el **binario instalador**
  apuntándolo a ese server con `OW_DEV_SERVER_URL` → **hot-reload del UI** con
  la API `installer` real (payload del binario), **sin re-empaquetar** los 55 MB.
- **Port de draw.io + instalador** en `examples/drawio/`: webapp real de draw.io
  sobre Owear, `owear.bridge.ts`, template del instalador scaffolded y build del
  instalador con el **comando** `ow build installer --mode minimal`.

## Changed

- **Template del instalador sin emojis hardcoded**: el plan de instalación
  muestra ruta + tamaño (antes `📁`/`📄`), y los controles de ventana
  (min/max/close) se cablean en el renderer.
- `ow build` (producción) **embebe `@owear/core`** en `app/main.js`
  (`prepareMain(..., bundle)`); en `ow dev` sigue externo. Necesario porque el
  payload de un single-binary/instalador **no lleva `node_modules`**.

## Fixed

- **Rutas con `~`**: `install`/`uninstall`/`verify`/`state`/`shortcuts` ahora
  **expanden `~`** (`$HOME`/`%USERPROFILE%`) y normalizan a **absoluto** — antes
  se creaba literalmente una carpeta `~` (instalación “en el sitio equivocado”).
- **Acceso directo real (Linux)**: `.desktop` con `Exec` **absoluto**,
  `Categories`/`StartupWMClass`, `chmod 0755`, **`update-desktop-database`**
  (aparece en el cajón), icono en `hicolor` y `~/Desktop` marcado como
  *trusted* (`gio set … metadata::trusted`).
- **Entorno al lanzar la app instalada**: al abrir la app desde el instalador se
  heredaban las `OW_*` del instalador (`OW_ASSETS_DIR`/`OW_APP_MAIN` apuntaban a
  su caché) y el lanzado cargaba **el UI del instalador** en vez de la app. Ahora
  se limpian (`OW_ASSETS_DIR`, `OW_APP_MAIN`, `OW_MODULES_DIR`, `OW_APP_WORKERS`,
  `OW_MODE`, `OW_APP_ID`, `OW_APP_VERSION`) antes de `execv` / `CreateProcessW`.

## Notas

- Verificado end-to-end: `ow build installer` produce instalador +
  desinstalador; instala y **la app instalada arranca con su propio payload**.
- Pendiente de pulir (en curso): progreso por fichero, selector de modo
  (`minimal`/`layout`) en el UI, icono de la app, y afinar el layout/ventana.
