# PicGo → Owear (port del PicGo REAL)

Este directorio es el **PicGo real** (su renderer React + TanStack Router +
Tailwind, sus routes/componentes/store) corriendo sobre **Owear** en lugar de
Electron. No es una reimplementación: se reutiliza el código de PicGo tal cual y
se sustituye **solo la capa Electron** (main + preload) por Owear.

## Arquitectura del port

```
proceso Electron                         →  en Owear
────────────────────────────────────────────────────────────────────────────
src/background.ts + src/main/*           →  app/main.ts        (sidecar Node)
   ipcMain.handle(canal, …)                  app.handle(canal, …)
   BrowserWindow / Tray / Menu / app         @owear/core (BrowserWindow, …)

src/preload/index.ts                     →  src/renderer/owear/bridge.ts
   contextBridge.exposeInMainWorld(            (shim en el renderer sobre
     'bridgeApi', { ipc, webUtils,               window.ow; MISMA forma de
       webFrame, env, i18n })                    BridgeApi)
```

- **`src/renderer/owear/bridge.ts`**: reproduce `window.bridgeApi`. `ipc.invoke/
  send/on` se reenvían al sidecar con `ow.invoke('node','call',{ fn, args })`;
  `env`/`webFrame`/`webUtils`/`i18n` se emulan en el cliente (con `@picgo/i18n`).
  Se importa el primero en `src/renderer/main.tsx`.
- **`app/main.ts`**: arranca la ventana (`app://index.html`) y registra los
  canales con `app.handle('<canal>', handler)`.
- **`src/main/**` y `src/universal/**`**: se conservan (lógica de PicGo
  reutilizable). `src/main/apis/**` (el core de PicGo) es la pieza clave a
  reutilizar desde `app/main.ts`.

## Estado

- ✅ El **renderer real de PicGo compila** con el toolchain de Owear
  (`vite.config.ts` → `ow build`) y **carga en el kernel** (probado: `app://
  index.html`, sidecar Node, bridge respondiendo).
- ✅ `ow build` produce `dist/` (renderer) + `dist/main.js` (sidecar).
- ⏳ **Porte de los canales IPC**: `src/main/events/ipcList.ts` (~250 canales) y
  `picgoCoreIPC.ts` a handlers de `app/main.ts` reutilizando `src/main/apis/**`.
  Los canales aún no portados devuelven `null` desde el shim (la UI arranca en
  modo degradado y se van habilitando features por canal).

## Uso

```bash
cd examples/picgo
pnpm install
pnpm dev            # ow dev: kernel + vite dev + sidecar node
# o:
node ../../packages/cli/src/ow.js build   # dist/ + dist/main.js
```

Para abrir el build: `OW_ASSETS_DIR=dist OW_APP_MAIN=dist/main.js ./owear`
(o `ow build app --format binary|deb|appimage` para un instalable).

## Origen

Código de PicGo (Molunerfinn/PicGo, licencia MIT — ver `LICENSE`), adaptado a
Owear. El árbol original se ha copiado tal cual para poder portar por capas.
