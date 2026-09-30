<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.4 (WIP / sin publicar)

Mejoras del **sistema de instaladores (D1)** y de su **template por defecto**, más
el port de **draw.io** como ejemplo con instalador propio. Todo en local; **no se
publica en npm** todavía.

## Auto-update

Sistema de actualización completo, con **delta**, **firma** y **manifiesto YAML**
(ver `docs/UPDATES.md`):

- **`autoUpdater` en el SDK** (`@owear/core`, proceso principal): `setFeedURL`,
  `checkForUpdates()`, `downloadUpdate()`, `quitAndInstall()` y eventos
  (`checking-for-update`, `update-available`, `update-not-available`,
  `download-progress`, `update-downloaded`, `error`). Providers `generic` y
  `github`; canales (`latest`, `beta`, …) y `mandatory`.
- **Delta por bloques** (estilo blockmap): el artefacto se parte en bloques y solo
  se descargan los **bloques cuyo sha256 cambia** (`Range: bytes=…`), reutilizando
  el resto del binario instalado. Agrupa bloques contiguos y cae a descarga
  completa si no hay blockmap.
- **Firma Ed25519** del manifiesto (sobre `"<version>:<sha256>"`), verificada con
  la clave pública del bridge (`updater.publicKey`); **integridad** sha256+sha512
  del artefacto (y por bloque al ensamblar).
- **Módulo nativo `updater`**: nuevas funciones `state()` (versión, `exe`, `dir`,
  `mode`, plataforma/arch, leyendo el **registro** de apps instaladas) y
  `apply({ path })` (reemplazo **atómico** del binario + **relaunch** del proceso).
- **Comando `ow update`** (+ `tools/owear-update.mjs`): `--gen-key` (par Ed25519) y
  publicación del canal → `<file>.blockmap` + `<channel>.yml` firmado. Flags:
  `--file`, `--version`, `--channel`, `--url`, `--key`, `--notes`, `--block-size`,
  `--mandatory`, `--out-dir`.
- **Tests**: `packages/core/test/updater.test.mjs` (YAML, semver, delta, firma) y
  `updater-feed.test.mjs` (tool real + HTTP `Range` + ensamblado y verificación);
  E2E `sdk.updater.state`.
- **Firma del binario** (`tools/owear-sign.mjs`, cableado en
  `ow build app|installer|uninstaller`): Ed25519 desprendida (`<file>.sig`) en
  cualquier plataforma y **Authenticode** (PE/MSI) vía `osslsigncode`/`signtool`
  si hay `--pfx`. Flags `--sign-key`, `--pfx`, `--pfx-password-env`,
  `--timestamp`, `--require-sign` (env `OW_SIGN_*`). El manifiesto añade
  `binarySig` (firma Ed25519 del artefacto completo), verificado por el updater.
- **Reintentos y reanudación**: `autoUpdater.maxRetries` / `retryDelay` /
  `requestTimeout`; backoff exponencial con jitter ante `408/425/429/5xx` y
  errores de red; descarga completa **reanudable** con `Range: bytes=<recibido>-`.
- **Rollback**: `updater.apply({ path, backup })` guarda `<exe>.owprev`;
  `updater.rollback()` restaura y relanza; `updater.commit()` confirma y borra el
  backup; `state().hasRollback`. En el SDK: `autoUpdater.rollback()`,
  `commitUpdate()`, `quitAndInstall({ backup })` y **guard de arranque**
  `armBootGuard()` (detecta crash loop y revierte). `autoInstallOnAppQuit` y
  `allowPrerelease` ya cableados.
- **Tests nuevos**: `updater-boot.test.mjs` (crash loop/rollback),
  `updater-sign.test.mjs` (Authenticode + Ed25519) y `updater-retry.test.mjs`
  (reintentos, resume por `Range`, timeout); E2E `tests/e2e/update_apply.py`
  (apply + backup + rollback + commit reales sobre copia del kernel).

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
- **El template Starter trae icono y lo usa**: `ow create` genera ahora
  `public/favicon.svg` (la marca de Owear) y `app/main.ts` lo aplica con
  `app.setIcon(...)` — resolviendo la ruta tanto en dev (`public/`) como
  empaquetado (`dist/` → `app/`). Es el **icono base de la app**; el SVG lo
  decodifica el kernel vía `window.setIcon` (GdkPixbuf, con soporte SVG).
- **`installer.defaultDir()`** y **`installer.chooseDir()`** (nativos en el
  builtin): el UI del instalador ya **no usa `dialog`** (un módulo `.owm`) →
  el instalador es **autocontenido** (solo builtins). `chooseDir` abre el
  selector de carpeta nativo (GTK en Linux, `IFileOpenDialog` en Windows).
- **Template del instalador con los estilos del Starter**: el instalador por
  defecto (`packages/cli/template-installer/`) usa ahora los tokens y
  componentes del app Starter de Owear — paleta oscura + acento rosado,
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

- **El template por defecto de `ow create` ahora es el Starter**: las apps
  generadas salen con la UI del Starter (paleta oscura + acento rosado, JetBrains
  Mono + Dancing Script self-hosted, titlebar propia, cards/consola) y el
  showcase de módulos nativos C1–C10 (app, dialog, fs, webContents, nativeImage,
  menu, tray, nativeTheme, print/printToPDF, BrowserWindow, screen, power) +
  navegador embebido (webview nativa).
- **Template del instalador sin emojis hardcoded**: el plan de instalación
  muestra ruta + tamaño (antes `📁`/`📄`), y los controles de ventana
  (min/max/close) se cablean en el renderer.
- `ow build` (producción) **embebe `@owear/core`** en `app/main.js`
  (`prepareMain(..., bundle)`); en `ow dev` sigue externo. Necesario porque el
  payload de un single-binary/instalador **no lleva `node_modules`**.

## Fixed

- **Auto-update (auditoría)**: se corrigieron varios fallos encontrados al
  revisar el updater:
  - un feed firmado (`binarySig`) **rompía** `checkForUpdates()` en apps sin
    `publicKey` configurada; ahora la firma es *best-effort* sin clave y
    obligatoria con clave (coherente con la doc);
  - `request()` **no liberaba la conexión** al reintentar un `5xx` (fuga de
    socket); ahora cancela el cuerpo antes del backoff;
  - un **blockmap relativo** no se resolvía contra el directorio del manifiesto;
  - el **desinstalador** interno del instalador se compilaba **sin los flags de
    firma** (`--sign-key`/`--pfx` no se propagaban);
  - `apply()` ahora **restaura el backup** si el relanzamiento (`execv`) falla,
    para no dejar el ejecutable en un estado que no arranca;
  - los hashes/firmas del manifiesto se citan siempre (evita que un valor “solo
    de dígitos” se reinterprete como número al parsear el YAML);
  - el delta emite un `download-progress` final al 100 %.

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
