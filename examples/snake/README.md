# 🐍 Owear Snake

Un Snake completo y jugable. No es un "hola mundo": el objetivo es mostrar en
código **por qué en Owear el IPC deja de ser el cuello de botella**.

## La idea

En Electron, la UI (renderer) está aislada del sistema: **todo** pasa por el
main process con `ipcRenderer.invoke` → IPC → `ipcMain.handle` → llamada nativa
→ vuelta. Cada `fs.readFile`, cada `dialog`, cada `net` es un mensaje
serializado entre procesos.

En Owear el renderer habla **directo** con el kernel:

```
Electron:  UI ──IPC──▶ main (Node) ──▶ nativo        (2 saltos, serialización)
Owear:     UI ─────────────────────▶ módulo nativo   (0 saltos por proceso)
```

Así que este juego:

- corre el bucle completo (input, física, render) en el renderer con
  `requestAnimationFrame` → **cero mensajes por frame**;
- guarda el récord llamando **directo** al módulo stock `fs`
  (`ow.invoke('fs','readText'/'writeFile', …)`);
- notifica el nuevo récord con el módulo stock `notification`;
- controla la ventana con el builtin `ow-window` (min/max/cerrar + drag nativo);
- y tiene un `app/main.ts` que **sólo crea la ventana**. Cero `ipcMain` y
  cero módulos nativos escritos a mano: todo es stock.

> Para ver **cuándo sí** escribir un módulo nativo propio, mira
> [`../cursor-xray`](../cursor-xray).

## Estructura

```
snake/
├── index.html          titlebar custom + canvas + HUD + overlay
├── src/renderer.ts     TODO el juego + llamadas nativas directas
├── app/main.ts         main OPCIONAL: sólo crea la ventana
└── vite.config.ts
```

## Ejecutar

Desde la **raíz del monorepo** (el kernel se compila la primera vez; requiere
el toolchain de `.github/workflows/ci.yml`, Linux hoy):

```bash
pnpm install
cd examples/snake
pnpm dev            # ≡ ow dev
```

- `ow dev` bundlea `app/main.ts`, levanta Vite y abre la ventana con el WebView
  del sistema.
- Para producción: `pnpm build` (≡ `ow build`).

## Lo que este ejemplo NO necesita (y en Electron sí)

| Necesidad | Electron | Este ejemplo |
|---|---|---|
| Leer/escribir el récord | `ipcMain.handle` + `ipcRenderer.invoke` | `ow.invoke('fs', …)` directo |
| Notificar | `ipcMain` → `Notification` | `ow.invoke('notification', 'show', …)` |
| Botones de ventana | preload + IPC + `BrowserWindow` | `ow.invoke('ow-window', …)` |
| Estado del juego | main o renderer + IPC | renderer puro (canvas) |
| Handler en el main | obligatorio | **ninguno** |

## Y para datos de alta frecuencia (streams, buffers grandes)

Ahí tampoco se mandan mensajes: Owear publica buffers en **memoria compartida**
y el renderer los lee como `ArrayBuffer` con `ow.readShared()` (`ow-shm://`),
sin copia ni base64. El bridge sólo transporta el *handle*. Para un juego no
hace falta; para un editor o un terminal, es la diferencia entre 60 fps y
stutter.
