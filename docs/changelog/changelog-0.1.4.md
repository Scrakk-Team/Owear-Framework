<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.4 (WIP / sin publicar)

**D1 — Instaladores modulares.** El instalador es **una app Owear aparte**
(HTML/CSS/JS, su propio `package.json` y opcionalmente sidecar Node) que lleva
embebido el payload de la app y un **bridge** de contrato. Se compila a un
**binario instalador** (Linux: binario; Windows: `.exe`) con `ow build installer`,
y el resultado instalado puede ser **un solo binario** (`minimal`) o un **árbol
de carpetas organizado** (`layout`). El desinstalador es otro binario aparte con
el mismo bridge. Todo se conecta por una API dedicada y un fichero puente.

> Esta entrada no está publicada en npm. Las versiones de los paquetes no se
> suben todavía.

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
  verify → uninstall` (instala/desinstala de verdad en el destino).
- Pendiente (fases 2–3): formatos `.deb`/`.AppImage` de la app, `.MSI` de
  Windows, `protect` con cifrado real, y hooks del bridge.
