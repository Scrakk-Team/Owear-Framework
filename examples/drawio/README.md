<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# draw.io → Owear (port de `drawio-desktop`)

Este directorio es **draw.io** (el renderer real de `jgraph/drawio-desktop`,
la webapp de draw.io tal cual) corriendo sobre **Owear** en lugar de Electron.
No es una reimplementación: se reutiliza la webapp y se sustituye **solo la capa
Electron** (main + preload).

## Arquitectura del port

```
Electron (drawio-desktop)                 →  en Owear
─────────────────────────────────────────────────────────────────────────────
src/main/electron.js (4541 líneas)        →  app/main.ts        (sidecar Node)
   ipcMain.on('rendererReq', …)              app.handle('drawio.req', …)
   BrowserWindow / Menu / dialog / …         @owear/core

preload (electron-preload.js)             →  owear-preload.js   (shim)
   contextBridge 'electron' {                  reproduce `window.electron`
     request/registerMsgListener/…              sobre `window.ow`

drawio/src/main/webapp (webapp)           →  webapp/  (se sirve como app://)
```

- **`owear-preload.js`**: reproduce `window.electron` (`request`,
  `registerMsgListener`, `sendMessage`, `listenOnce`) sobre `window.ow`; cada
  `request` hace `ow.invoke('node','call',{ fn:'drawio.req', args:[msg] })`.
  Se inyecta en `webapp/index.html` antes de `js/main.js`.
- **`app/main.ts`**: replica el switch `rendererReq` de drawio-desktop
  (`saveFile`, `readFile`, `showOpenDialog`, `windowAction`, `openExternal`,
  `clipboardAction`, `fileStat`, …) usando `@owear/core` + Node `fs`.

## Setup y ejecución

La webapp de draw.io pesa **157 MB** → **no se commitea** (ver `.gitignore`).
`setup.mjs` la trae del repo `jgraph/drawio` (sparse: solo `src/main/webapp`).

```bash
cd examples/drawio
pnpm install          # @owear/core (link) + esbuild
node setup.mjs        # trae webapp/ + inyecta el shim
node run.mjs          # compila app/main.ts y arranca el kernel Owear
```

`node run.mjs` compila `app/main.ts` → `.owear/main.js` (esbuild) y lanza el
kernel con `OW_ASSETS_DIR=webapp`, `OW_APP_MAIN=.owear/main.js`.
Para el kernel: compílalo (`cmake --preset linux-release && cmake --build …`) o
define `OW_KERNEL_BIN`.

## Estado

- ✅ La **webapp real de draw.io** se sirve en Owear (`app://index.html`).
- ✅ Protocolo renderer↔main replicado (`drawio.req`): abrir/guardar/leer/
  escribir/stat/dialog/clipboard/ventana/abrir-enlace…
- ⏳ Pendiente: **menú nativo** (drawio usa un menú Electron grande),
  `watchFile`, borradores/backup (`electron-store`) y autoupdate.

## Origen

`jgraph/drawio-desktop` + `jgraph/drawio` (licencias en `LICENSE.drawio*`),
adaptados a Owear.
