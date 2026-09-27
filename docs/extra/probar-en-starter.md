<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Probar todos los sistemas de Owear en el starter

> **WIP — se irá rellenando poco a poco.** Este documento es la guía viva de
> cómo probar, **en el starter** (`examples/starter`), cada sistema que va
> entrando en Owear. Por ahora están los pasos y el estado; los botones/paneles
> concretos del starter se irán añadiendo.

Cómo leer la tabla de estado:

- ✅ **Verificado** — probado end-to-end (en la plataforma indicada).
- 🟡 **Parcial** — implementado; falta el botón/panel en el starter.
- ⏳ **Pendiente** — aún no implementado o no probado en el starter.

---

## 0. Arranque del starter

```bash
# Linux
cd examples/starter
npm run dev
```

En **Windows** (kernel cruzado + pull por HTTP):

```powershell
taskkill /IM owear.exe /F 2>$null
$dst = "$env:USERPROFILE\owear-dev\deps\win32-x64"
Remove-Item -Recurse -Force $dst -ErrorAction SilentlyContinue
iwr http://<host-linux>:8000/win-out.zip -OutFile $env:TEMP\win-out.zip
Expand-Archive -Force $env:TEMP\win-out.zip $dst
$env:OW_KERNEL_BIN = "$dst\bin\owear.exe"
cd <ruta-al-starter>
npm.cmd run dev
```

---

## 1. Bloque A — ejecución / IPC

| Sistema | Qué demuestra | Estado Linux | Estado Windows |
|---|---|---|---|
| `app.forkWorker` | worker Node con canal (reemplazo de `utilityProcess.fork`) | ✅ (SDK test + CLI) | 🟡 (compila) |
| `windowId` en handlers | `app.handleContext((ctx) => ctx.windowId)` | ✅ | 🟡 |
| `webContents.send` | `win.webContents.send(name, payload)` dirigido | ✅ | 🟡 |
| `MessageChannel` | `app.createChannel()` + `ow.port` (main↔renderer) | ✅ | 🟡 |
| N-API | addons nativos (`docs/NATIVE.md`) | ✅ (docs) | ⏳ |

### 1.1 Worker Node con canal

**Main** (`app/main.ts`):

```ts
const w = app.forkWorker('echo-worker.js')   // app/workers/echo-worker.ts
w.on('message', (m) => console.log('worker →', m))
w.postMessage({ ping: 1 })
```

**Worker** (`app/workers/echo-worker.ts`), estilo Electron:

```ts
process.parentPort.on('message', (e) => process.parentPort.postMessage({ echo: e.data }))
```

- El CLI compila `app/workers/**` → `.owear/workers/**` y expone `OW_APP_WORKERS`.
- **En el starter:** panel "Workers" con botón *ping* y log de respuestas. ⏳

### 1.2 `windowId` en handlers

```ts
app.handleContext('whoami', (ctx) => ({ windowId: ctx.windowId }))
```

- **En el starter:** botón que muestra `ctx.windowId` y la ventana que lo pidió. ⏳

### 1.3 `webContents.send`

```ts
win.webContents.send('hello', { n: 1 })   // renderer: ow.on('hello', cb)
```

- **En el starter:** botón que manda un evento solo a su ventana. ⏳

### 1.4 MessageChannel / MessagePort

```ts
const ch = app.createChannel()
ch.port1.on('message', (m) => console.log('renderer →', m))
await app.sendPort(win.id, 'ow:port', ch.port2)
```

```ts
ow.on('ow:port', ({ port }) => {
  const p = ow.port(port)
  p.on('message', (m) => p.postMessage({ pong: m }))
})
```

- **En el starter:** panel "Puerto" con ping/pong y contador. ⏳

---

## 2. Bloque B — protocolo / datos / OS

| Sistema | Qué demuestra | Estado Linux | Estado Windows |
|---|---|---|---|
| `app.protocol` (handler) | esquema servido por el main (`scrakk-ext://…`) | ✅ | 🟡 (compila) |
| `app.protocol` (serve) | el kernel sirve un directorio | ✅ | 🟡 |
| `safeStorage` | cifrado por el SO | ✅ | 🟡 (compila) |
| `theme` (nativeTheme) | claro/oscuro + forzar + evento | ✅ | 🟡 (compila) |
| `session` permisos | geolocation/notifications/… | ✅ (geolocation) | 🟡 (compila) |
| `session` particiones | `session.fromPartition` (perfiles) | ✅ | 🟡 (compila) |
| `session` webRequest | `onBeforeRequest` (cancelar/redirigir) | ✅ (navegaciones) | 🟡 (compila; todos los requests) |

### 2.1 `protocol` con handler (contenido dinámico)

```ts
await app.protocol('scrakk-ext', {
  privileged: { secure: true, cors: true },
  handler: async (req) => new Response('<h1>hola</h1>', {
    headers: { 'content-type': 'text/html' },
  }),
})
win.loadURL('scrakk-ext://extid/view/x')
```

- **En el starter:** pestaña que carga un `app://`-like servido por el main. ⏳

### 2.2 `protocol` con `serve` (directorio)

```ts
await app.protocol('assets', { serve: '/ruta/al/dir' })
```

- **En el starter:** cargar `assets://x/index.html`. ⏳

### 2.3 `safeStorage`

```ts
const { data, encrypted } = await safeStorage.encrypt('token')
const text = await safeStorage.decrypt(data)
```

- Windows: DPAPI · Linux: AES-256-GCM (clave en el data dir, 0600).
- **En el starter:** campo de texto + botones cifrar/descifrar + badge `encrypted`. ⏳

### 2.4 `theme` (nativeTheme)

```ts
console.log(await theme.get())     // { dark, source, highContrast, reducedTransparency }
await theme.setSource('dark')      // system | light | dark
theme.watch()                      // → ow.on('theme.changed', cb)
```

- **En el starter:** toggle system/light/dark + lectura de `prefers-color-scheme`. ⏳

### 2.5 `session` permisos

```ts
session.onPermissionRequest(({ permission, origin }) => permission === 'geolocation')
```

- Trigger en renderer: `navigator.geolocation.getCurrentPosition(...)`.
- **En el starter:** botón que pide geolocalización y muestra allow/deny. ⏳

### 2.6 `session` particiones (perfiles)

```ts
const s = session.fromPartition('persist:cuenta-2')
new BrowserWindow({ session: s.partition, url })   // perfil aislado
```

- Verificado en Linux: `persist:a` y `persist:b` **no** comparten `localStorage`;
  dos ventanas de `persist:a` sí.
- **En el starter:** botón "abrir en perfil 2" que abre una ventana con otra
  partición y compara `localStorage`. ⏳

### 2.7 `webRequest` (intercepción)

```ts
webRequest.onBeforeRequest({ urls: ['*://example.com/*'] }, () => ({ cancel: true }))
// o redirigir:
webRequest.onBeforeRequest({ urls: ['<all_urls>'] }, (d) =>
  d.url.startsWith('http://') ? { redirectURL: d.url.replace('http://', 'https://') } : undefined
)
```

- **Windows**: intercepta todos los requests. **Linux**: navegaciones (WebKitGTK
  2.52 ya no expone `send-request` para subrecursos).
- Verificado en Linux: `location.href='https://example.com/'` queda bloqueado.
- **En el starter:** panel con un input de patrón + botón "bloquear" y contador
  de requests interceptados. ⏳

---

## 3. Notas de verificación (Linux, Xvfb)

Los E2E de estos sistemas se pueden correr headless:

```bash
Xvfb :99 -screen 0 1280x800x24 &
DISPLAY=:99 OW_APP_MAIN=... OW_MODULES_DIR="$(ls -d build/linux-release/api/*/ | grep -v CMakeFiles | tr '\n' ':')" \
  ./build/linux-release/src/owear
```

- Los módulos `.so` viven en `build/linux-release/api/<nombre>/<nombre>.so`.
- `OW_ASSETS_DIR` apunta al dir con `index.html` para servir `app://`.

---

## 5. Bloque C — shell de app

### 5.1 C1 — `app` (rutas, identidad, commandLine, eventos)

| | Estado |
|---|---|
| Linux | ✅ **verificado** (rutas, `setPath`, `getName/getVersion/isPackaged/getAppPath`, `commandLine`, `window-all-closed`/`before-quit`/`will-quit`) |
| Windows | 🟡 compila + script de prueba abajo |

```ts
// app/main.ts
app.on('window-all-closed', () => console.log('window-all-closed'))
app.on('before-quit', () => console.log('before-quit'))
app.on('will-quit', () => console.log('will-quit'))

app.whenReady().then(() => {
  for (const n of ['home', 'userData', 'temp', 'logs', 'downloads', 'exe', 'appPath'])
    console.log('path', n, '=', app.getPath(n))
  console.log('name', app.getName(), 'version', app.getVersion(), 'packaged', app.isPackaged())
  app.commandLine.appendSwitch('use-gl', 'angle')
  console.log('switch use-gl =', app.commandLine.getSwitchValue('use-gl'))
})
```

**En Windows** (tras el pull del kernel), ejecuta el starter/dev de tu app y busca
en la consola `path …`, `name …` y los eventos al cerrar/`app.quit()`. En Windows
`userData` debe ser `%LOCALAPPDATA%\<OW_APP_ID>` y `exe` la carpeta de `owear.exe`.

- **En el starter:** panel **`App (C1)`** (nombre, versión, empaquetada,
  `userData`/`exe`/`appPath`) + eventos de ciclo de vida en la consola.
  **Cableado** (el renderer llama `starter.appInfo`/`starter.appPaths` por el
  puente Node; el main reenvía `app.event`). WIP, sin commitear. 🟡

---

### 5.2 C2 — `dialog` completo

| | Estado |
|---|---|
| Linux | ✅ registrado (5 funciones); los diálogos son modales (no automatizables) |
| Windows | 🟡 compila (`IFileDialog` + `TaskDialogIndirect`) |

```ts
// abrir varios
const r = await ow.invoke('dialog', 'showOpenDialog', {
  title: 'Abrir varios',
  properties: ['openFile', 'multiSelections'],
  filters: [{ name: 'Texto', extensions: ['txt', 'md'] }],
})                       // → { canceled, filePaths }
// guardar
await ow.invoke('dialog', 'showSaveDialog', { defaultPath: 'x.txt' }) // → { canceled, filePath }
// mensaje con botones + checkbox
await ow.invoke('dialog', 'showMessageBox', {
  type: 'question', message: '¿Continuar?', buttons: ['Sí', 'No'],
  checkboxLabel: 'No volver a preguntar',
})                       // → { response, checkboxChecked }
```

- **En el starter:** botones **Abrir varios…**, **Guardar como…**, **Diálogo con
  botones** (cableados). 🟡

---

## 4. Pendiente de documentar aquí

- Botones/paneles concretos del starter para cada sistema (lo iremos añadiendo).
- Particiones de sesión y `webRequest`.
- Prueba en **Windows** de todo lo marcado 🟡 (cuando haya hardware).
