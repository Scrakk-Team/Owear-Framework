<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Módulos nativos (N-API) en Owear

Owear **no embebe un motor Node propio**: el proceso principal (sidecar) corre en
un **Node real** resuelto por el Runtime Manager (`app.ensureNodeRuntime()`), con
prioridad `OW_NODE_BIN → Node del sistema → caché → descarga oficial`. Eso hace
que los addons **N-API** funcionen sin capa de compatibilidad.

## Qué funciona tal cual

- Addons construidos con **N-API** (`node-addon-api`, `napi-rs`, `prebuildify`):
  son ABI-estables entre versiones de Node → cargan sin recalcular.
- Paquetes con **prebuilds de Node** (la mayoría: `better-sqlite3`, `sharp`,
  `sqlite3`, `keytar`…).
- `child_process`, `worker_threads`, `net`, `crypto`, `fs`… (Node estándar).
- Los **workers** de Owear (`app.forkWorker`) corren en el mismo Node → pueden
  cargar addons igual que el main.

## Qué NO funciona sin rebuild

- Binarios precompilados **para Electron** (los que instala `electron-rebuild` o
  `@electron/rebuild`). Electron tiene su propio ABI; Node no lo comparte.
  Si vienes de Electron, **recompila** para Node:

  ```bash
  npm rebuild                    # reconstruye contra el Node actual
  # o, para uno concreto:
  npm_config_runtime=node npm_config_target=$(node -p process.versions.node) npm rebuild
  ```

- Módulos que asumen APIs de Electron (`electron`, `app`, `process.parentPort`,
  `utilityProcess`). En Owear el shim de `process.parentPort` existe para
  workers, pero `require('electron')` no. Migra esas partes a `@owear/core`.

## Ejemplo (addon N-API desde el main)

```ts
import { app, BrowserWindow, invokeNative } from '@owear/core'
import Database from 'better-sqlite3' // prebuild N-API

app.whenReady().then(() => {
  const db = new Database('cache.db')
  db.exec('create table if not exists kv (k text primary key, v text)')
})
```

`better-sqlite3` se instala con su prebuild de Node y carga directo; no hace
falta nada del kernel.

## Empaquetado (`ow build`)

El runtime Node se resuelve en destino (no se copia un Node por app). Los
`.node` que instala tu app viven en `node_modules/` y viajan con la app. Para
distribuir, asegúrate de:

1. Compilar/instalar contra un **Node compatible** con el que resuelva el
   usuario (N-API evita este problema).
2. No empaquetar binarios de Electron.
3. Si tu addon trae prebuilds por plataforma, incluir la del destino
   (`linux-x64`, `win32-x64`).

## Workers y addons

```ts
const w = app.forkWorker('heavy-worker.js')
w.postMessage({ op: 'init' })
```

El worker corre con el mismo Node → puede hacer `require('sharp')` o cargar su
propio `.node`. Ver `docs/` y el changelog de la versión para más detalle.
