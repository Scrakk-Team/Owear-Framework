<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# API Reference completa — Owear 0.1.0

> Mantenida a mano contra el árbol de trabajo (no autogenerada): puede quedar
> por detrás. La fuente de verdad es la tabla de funciones de cada módulo
> (`api/<módulo>/src/*.cpp`), `src/Core/WindowModule.cpp` para los builtins y
> `packages/core/src/index.ts` para el SDK. Toda llamada del renderer pasa por
> `window.ow`; toda API Node habla con el kernel por el Control Socket.
>
> Última revisión: 2026-09 (§2.1 `fs` sincronizada con las 26 funciones reales).

---

## 1 · Renderer — `window.ow` (inyectado por el kernel en cada documento)

```ts
ow.invoke(module: string, fn: string, ...args): Promise<unknown>
  // rechaza con 'ow: timeout <módulo>/<fn>' si el kernel no responde en 30 s
ow.invokeSync(module: string, fn: string, ...args): unknown        // ⚠️ bloquea el renderer
ow.readShared(handle: { id: string; size: number }): Promise<ArrayBuffer>
  // para payloads ≥256 KB: el kernel los publica en memoria compartida y
  // el renderer los lee sin pasar por JSON/base64 (scheme ow-shm://)
ow.on(name: string, cb: (payload: unknown) => void): () => void    // devuelve unsubscribe
ow.emit(name: string, payload?: unknown): void                     // JS → nativo + listeners JS
ow.emitTo(targetWindowId, name, payload?)                          // IPC dirigido ventana→ventana
ow.findInPage(text, opts?): { matches, active }                    // helper JS (window.find)
window.__owWindowId: number                                        // id de la ventana actual
```

Schemes disponibles desde el WebView:

| Scheme | Uso |
|---|---|
| `app://<ruta>` | assets del bundle (dist/) |
| `ow-shm://<id>` | región de memoria compartida sin copia |
| `ow-sync://i/<payload>` | invoke síncrono (interno de `invokeSync`) |

Atributos DOM reconocidos por el kernel:

```html
<div data-ow-drag>…</div>                          <!-- zona de arrastre de titlebar -->
<button data-ow-no-drag>…</button>                 <!-- excluye del drag -->
<div data-ow-resize="bottom-right">…</div>         <!-- resize manual frameless -->
<!-- edges: left|right|top|bottom|top-left|top-right|bottom-left|bottom-right -->
```

---

## 2 · Módulos nativos — invocables con `ow.invoke('<módulo>', '<fn>', …args)`

## 2.1 `fs` (stock, builtin + .owm)

```ts
// ── básicos ──
fs.readText(path): Promise<string>
fs.readFile(path): Promise<{ b64: string } | { __ow_shm: { id: string; size: number } }>
//                                    ^ <256 KB      ^ ≥256 KB → leer con ow.readShared()
fs.writeFile(path, data: string, encoding?: 'utf8' | 'base64'): Promise<null>
//  crea los directorios padre que falten
fs.readDir(path): Promise<{ name: string; type: 'file' | 'dir' | 'other' }[]>
fs.stat(path): Promise<{ size: number; isFile: boolean; isDir: boolean; mtimeMs: number } | null>
fs.mkdir(path, recursive?: boolean): Promise<null>
fs.remove(path, recursive?: boolean): Promise<null>   // recursive → remove_all
fs.exists(path): Promise<boolean>

// ── metadatos ──
fs.copy(src, dest): Promise<null>
fs.rename(oldPath, newPath): Promise<null>
fs.chmod(path, mode): Promise<null>                    // mode octal numérico
fs.symlink(target, linkPath): Promise<null>
fs.readlink(path): Promise<string>
fs.lstat(path): Promise<{ isSymlink: boolean; isFile: boolean; isDir: boolean,
                          size: number; mtimeMs: number } | null>   // NO sigue symlinks
fs.realpath(path): Promise<string>
fs.mkdtemp(prefix?): Promise<string>
fs.access(path): Promise<boolean>                      // legible por el proceso
fs.truncate(path, size): Promise<null>
fs.utimes(path, atimeMs, mtimeMs): Promise<null>

// ── handles fd-style (archivos grandes, lectura por trozos) ──
fs.open(path, flags?: 'r' | 'w' | 'a' | 'r+' | 'w+'): Promise<{ fd: number }>
fs.read(fd, length?): Promise<{ b64: string; eof: boolean }
                            | { __ow_shm: { id: string; size: number }; eof: boolean }>
fs.write(fd, data: string | { __ow_shm }, offset?): Promise<number>
fs.size(fd): Promise<number>
fs.close(fd): Promise<null>

// ── watch ──
fs.watch(path): Promise<null>       // emite el evento 'fs.watch' con el path cambiado
fs.unwatch(path): Promise<null>
```

## 2.2 `window` (builtin — extras de ventana, F8)

```ts
window.openDevTools(windowId, show?)           // inspector WebKit
window.capturePage(windowId) → {__ow_shm:{id,size}, format:'png'}
window.setAlwaysOnTop(windowId, bool); window.isAlwaysOnTop(windowId)
window.setOpacity(windowId, 0..1)
window.flashFrame(windowId, bool)              // urgencia taskbar/dock
window.setIcon(windowId, pngB64)
window.setUserAgent(windowId, ua)
window.zoom(windowId, factor)                  // 1.0 = 100%

// ── navegación (F-next) ──
window.reload(windowId); window.stop(windowId)
window.goBack(windowId); window.goForward(windowId)
window.canGoBack(windowId); window.canGoForward(windowId)
window.getURL(windowId) → string; window.getTitle(windowId) → string
window.findInPage(windowId, text, {matchCase?, backwards?}?) → {matches, active}
window.findStop(windowId)
// eventos por ventana (vía SDK): navigationStarted · loadCommitted ·
// didFinishLoad · didFailLoad{url,code,description} · pageTitleUpdated{title}
```

## 2.3 `session` (builtin — cookies/cache/proxy/downloads)

```ts
session.cookiesGet(url, name?, domain?)
  → {cookies: [{name,value,domain,path,httpOnly,secure}]}
session.cookiesSet({name, value, domain, path?, maxAge?})
session.cookieDelete(url, name) → bool
session.clearStorage(windowId, {cookies?, localStorage?}?)   // + cache siempre
session.setProxy(windowId, proxyUrl | 'system')
session.attach(windowId)                       // habilita eventos de descargas
session.downloadCancel(downloadId)
// eventos: session.downloadStarted{downloadId,destination}
//          session.downloadProgress{downloadId,progress 0..1}
//          session.downloadFinished{downloadId}
session.setUserAgentAll(ua)                    // aplica a todas las ventanas vivas
session.spellCheck(windowId, lang1, lang2…)    // sin langs = off
```

## 2.4 `crashreporter` (builtin)

```ts
crashreporter.install() → crashDir     // SIGSEGV/ABRT/FPE/BUS/ILL → log+backtrace
crashreporter.lastCrashLog() → string | null
```

## 2.5 `net` (builtin)

```ts
net.request({ method:'GET'|'POST'|'PUT'|'DELETE', url,
              headers?: {k:v}, body?: string, bodyB64?: string,
              timeoutMs?: number })
  → { status, headers:{minúsculas}, body:string|__ow_shm(≥256KB) }
```

## 2.6 `app` (builtin)

```ts
app.setBadgeCount(n)              // Unity launcher; otros DE → error claro
app.requestSingleInstanceLock() → bool   // false = ya hay instancia (envía argv
                                         // y emite 'secondInstance' en la 1ª)
app.relaunch()                    // execv del propio binario
```

## 2.7 `capturer` (.owm — X11 v1)

```ts
capturer.getSources() → [{type:'screen', id, name, bounds, thumbnail:{id,size}}]
capturer.captureScreen(screenIndex=0) → {__ow_shm, width, height, format:'png'}
// ⚠ Wayland no soportado v1 (error claro); usar GDK_BACKEND=x11 o portal en F-next
```

## 2.8 `ow-window` (builtin interno — titlebar y ciclo de vida)

```ts
owWindow.minimize(windowId): Promise<null>
owWindow.maximize(windowId, enabled: boolean): Promise<null>
owWindow.close(windowId): Promise<null>            // dispara closeRequested (vetable)
owWindow.focus(windowId): Promise<null>
owWindow.setTitle(windowId, title: string): Promise<null>
owWindow.isMaximized(windowId): Promise<boolean>
owWindow.respondCloseRequest(windowId, requestId: number, allow: boolean): Promise<null>

// Resueltos por el bridge ANTES del dispatcher (necesitan la ventana
// INVOCANTE, no la de windowId) y sin windowId:
owWindow.beginMoveDrag(): Promise<null>                       // arrastrar la ventana
owWindow.beginResizeDrag(edge: string): Promise<{edge: string}>
//  edge ∈ left|right|top|bottom|top-left|top-right|bottom-left|bottom-right
//  Un borde desconocido se rechaza SIN iniciar el drag, y la respuesta ecoa
//  el borde efectivo (para verificar el parseo sin depender del WM).
```

`ow.invoke('ow-window', 'close', id)` es vetable: el kernel emite
`closeRequested {requestId}` al renderer y espera `respondCloseRequest`; si
nadie responde en `OW_CLOSE_TIMEOUT_MS` (default 1000 ms) cierra igualmente.
El renderer debe responder con el `requestId` recibido (un id desconocido es
un no-op, no un error).

## 2.9 `node` (builtin — puente al proceso principal / Node)

Permite al renderer **usar Node** (p. ej. el extension host de VS Code) **sin
IPC por defecto**: solo las features que lo necesitan pagan el salto extra.

```ts
// main (app/main.ts): registra handlers
app.handle('extension.activate', (id) => { /* … */ return { ok: true } })

// renderer: los invoca y recibe eventos
const r = await ow.invoke('node', 'call', { fn: 'extension.activate', args: [id] })
ow.on('extension.event', (payload) => { /* … */ })
```

```ts
// main → renderer (opcional: windowId para dirigir a una ventana)
app.send('extension.event', { … })
```

Resolución **asíncrona**: el kernel reenvía `node/call` al main por el control
socket (`node.request`) y el main responde con `node.respond`; los eventos del
main van con `node.emit` → `ow.on(name)`. El resto de la API (fs, process/pty…)
sigue yendo **directo renderer → kernel → módulo**, sin pasar por Node.

## 2.10 Módulos propios (.owm) — ABI-C

```cpp
#include <ow/Json.h>
#include <ow/Module.h>

static void miFuncion(const ow_request_t* req, ow_response_t* res) {
    // req:  json (array de args), bin/bin_len, window_id, host
    // res:  status(0=ok), error, json, bin/bin_len
}

OW_MODULE_BEGIN(miModulo, "1.0.0")
OW_FN(miFuncion)
OW_MODULE_END()
```

API disponible dentro de un módulo:

| Header | Contenido |
|---|---|
| `ow_api.h` | `ow_module_desc_t`, `ow_fn_entry_t`, `ow_request_t`, `ow_response_t`, `OW_MODULE_EXPORT` |
| `ow/Module.h` | `OW_MODULE_BEGIN` / `OW_FN` / `OW_MODULE_END`, `Module::RespondOk(res, json, bin?)`, `Module::RespondError(res, msg)` |
| `ow/Shm.h` | `ow_shm_put(data,len)→id`, `ow_shm_data(id,&len)→ptr`, `ow_shm_shutdown()` (símbolos del host, `-rdynamic`) |
| `ow/Base64.h` | `b64::Encode(string_view)`, `b64::Decode(sv, out)` |
| `ow/Json.h` | `json::Parse(sv)→ParseResult`, `Value` (Null/Bool/Int/Double/String/Array/Object), `.Find()/.As*/.Serialize()`, `json::JsLiteral()` |

Contrato: los buffers de `res` viven solo durante la llamada (el host copia al
instante); prohibido lanzar excepciones hacia el host.

---

## 3 · SDK Node — `@owear/core` (main process estilo Electron)

```ts
import { app, BrowserWindow, platform } from '@owear/core'
```

### app

```ts
app.whenReady(): Promise<void>                       // conecta con el kernel
app.quit(exitCode?): Promise<void>
app.info(): Promise<{ pid; version; socket }>
app.ensureNodeRuntime(range?: string): Promise<{ path; version; source }>
  // 'latest'|'lts'|'v22'|…   source: env|system|cache|downloaded
app.handle(fn, handler): () => void
  // registra un handler invocable desde el renderer con
  // ow.invoke('node','call',{fn,args}); devuelve unsubscribe
app.send(name, payload?, windowId?): Promise<void>
  // empuja un evento a los renderers (ow.on(name)); windowId opcional
app.__channel                                        // acceso crudo al canal
```

### BrowserWindow

```ts
new BrowserWindow(options?: WindowOptions)

// opciones
interface WindowOptions {
  title?: string
  width?: number; height?: number
  x?: number; y?: number
  resizable?: boolean
  frameless?: boolean
  titleBarStyle?: 'default' | 'hidden' | 'custom'
  url?: string
}

// métodos
win.id: number | null
win.loadURL(url): Promise<void>
win.eval<T>(js): Promise<T>                          // resultado JSON-serializado
win.show(); win.hide(); win.focus()
win.close()                                          // vetable
win.destroy()                                        // inmediato
win.minimize(); win.maximize(); win.unmaximize()
win.setFullScreen(enabled)
win.isMaximized(): Promise<boolean>; win.isMinimized(): Promise<boolean>
win.getBounds(): Promise<Bounds>; win.setBounds(partial): Promise<void>
win.setTitle(title): Promise<void>
win.closeRespond(requestId: number, allow: boolean): Promise<void>   // F3.4

// eventos (win.on / win.once)
'ready-to-show' | 'closed' | 'resize'(Bounds) | 'move'(Bounds)
'focus' | 'blur' | 'maximize' | 'unmaximize'
'enterFullScreen' | 'leaveFullScreen'
'closeRequested'({ requestId }) | 'disconnected' | 'error'
```

`closeRequested` permite vetar el cierre desde el main:

```ts
win.on('closeRequested', ({ requestId }) => {
  if (hayCambiosSinGuardar) win.closeRespond(requestId, false)  // cancela
  else win.closeRespond(requestId, true)                        // cierra
})
```

### Módulos nativos desde el main

```ts
import { invokeNative, listNativeModules } from '@owear/core'

listNativeModules(): Promise<{ name; version; origin; builtin; functions; functionNames }[]>
nativeModuleInfo(name): Promise<{ name; version; origin; builtin; functions; functionNames }>
invokeNative<T>(module, method, ...args): Promise<T>
// await invokeNative('fs', 'readText', '/etc/hostname')
// los builtins de ventana esperan [windowId, …]:
//   await invokeNative('ow-window', 'setTitle', win.id, 'Hola')
```

---

## 4 · Protocolo de Control (kernel ↔ clientes NDJSON)

UDS `$XDG_RUNTIME_DIR/owear-<pid>.sock` (Linux/macOS) · named pipe
`\\.\pipe\owear-<pid>` (Windows). Formato:
`{"id":N,"cmd":"…","params":{…}}` → `{"id":N,"ok":bool,"result":…|"error":…}`

| Comando | Params |
|---|---|
| `app.info` | — |
| `app.quit` | `{exitCode?}` |
| `node.ensure` | `{range}` → `{path, version, source}` (`source`: `env`\|`system`\|`cache`\|`downloaded`) |
| `module.list` | — → `[{name, version, origin, builtin, functions, functionNames}]` (registry de módulos) |
| `module.info` | `{name}` → el objeto de un módulo concreto, o error si no existe |
| `module.invoke` | `{module, method, args?: unknown[], windowId?}` → resultado del módulo |
| `event.emit` | `{name, payload?, windowId?}` → reemite como `sdk.event` |
| `window.create` | `WindowOptions` completo |
| `window.close / destroy / show / hide / focus / minimize / maximize{enabled} / unmaximize / setFullScreen{enabled}` | `{windowId}` |
| `window.isMaximized / isMinimized` | `{windowId}` → bool |
| `window.getBounds` | `{windowId}` → `{x,y,width,height}` |
| `window.setBounds` | `{windowId, x?, y?, width?, height?}` |
| `window.setTitle` | `{windowId, title}` |
| `window.loadURL` | `{windowId, url}` |
| `window.respondCloseRequest` | `{windowId, requestId, allow}` |
| `window.eval` | `{windowId, js}` → JSON (respuesta async) |

Eventos kernel→cliente:

```
{"event":"window.event","params":{"windowId":N,"name":"…","payload":…}}
{"event":"sdk.event","params":{…}}
```

Nombres de evento de ventana: `resize move focus blur maximize unmaximize
enterFullScreen leaveFullScreen closeRequested{requestId} closed`.

`closeRequested` llega **también al SDK**: el main puede vetar el cierre con
`win.closeRespond(requestId, false)`. Si nadie responde en
`OW_CLOSE_TIMEOUT_MS` (default 1000 ms), el kernel cierra igualmente.

`module.invoke` es lo que permite al **proceso principal** (Node) usar los
módulos nativos —`fs`, `process`, `net`, `clipboard`…— igual que el renderer,
sin depender de una ventana. Los `args` viajan como array JSON, el mismo
formato que usa el bridge. Desde el SDK:

```ts
import { invokeNative, listNativeModules } from '@owear/core'

const mods = await listNativeModules()                 // [{name, version, origin, functions, …}, …]
const txt  = await invokeNative('fs', 'readText', '/etc/hostname')
```

Ojo: los builtins de ventana esperan `[windowId, …]` dentro de los args, p. ej.
`invokeNative('ow-window', 'setTitle', [win.id, 'Hola'])`.

---

## 5 · API C++ nativa — apps nativas-first (`include/ow/`)

```cpp
#include <ow/App.h>
ow::App::Main(argc, argv, AppOptions{ .id, .name, .version })
ow::App::OnReady(fn)          // crea ventanas aquí
ow::App::Post(fn)             // callback al main loop (thread-safe)
ow::App::Quit(exitCode)

#include <ow/Window.h>
ow::Window w(WindowOptions{
  .title, .width, .height, .minWidth/Height, .maxWidth/Height,
  .resizable, .center, .show, .frameless,
  .titleBarStyle = TitleBarStyle::Default|Hidden|Custom,
  .titleBarOverlay = { color, symbolColor, height },
  .url,                     // http(s):// · app:// · file://
  .webviewArgs              // flags extra del navegador
});
w.Id(); w.Show(); w.Hide(); w.Close(); w.Destroy(); w.Focus();
w.Minimize(); w.Maximize(); w.Unmaximize(); w.Restore();
w.SetFullScreen(bool); w.IsMaximized/Minimized/FullScreen();
w.GetBounds() / w.SetBounds({x,y,w,h}); w.Center();
w.SetTitle(s) / w.Title(); w.SetTitleBarStyle(); w.SetTitleBarOverlay();
w.LoadURL(url);
w.EvalJS(js, cb(resultJson));       // resultado como JSON
w.EmitToJS(name, jsonPayload);      // evento a suscriptores JS
w.On(name, fn(payload)) -> ListenerId;  w.Off(id);
w.NativeHandle();                   // GtkWidget*/HWND/NSView*

#include <ow/Shm.h>                 // memoria compartida (C puro)
const char* ow_shm_put(const uint8_t*, size_t);
const uint8_t* ow_shm_data(const char* id, size_t* len);
void ow_shm_shutdown(void);

#include <ow/Common.h>              // Result<T>, OwBytes, WindowId, NonCopyable
```

Internos del kernel (para contribuidores, en `src/`): `bridge::Codec`,
`Dispatcher`, `ModuleLoader`, `ControlServer`, `NodeManager`, `http`,
`archive::ExtractTarGz`, `crypto::Sha256`, `shm::Put/Data`.

---

## 6 · CLI

```bash
ow create <dir>     # scaffoldea una app desde el template
ow create installer [dir]    # scaffoldea el instalador (app Owear en modo installer)
ow create uninstaller [dir]  # scaffoldea el desinstalador
ow dev              # kernel + vite dev server + sidecar node (hot reload)
ow build            # vite build + native/*.cpp → dist/modules/*.owm
ow build app        # payload de la app (--format binary|deb|appimage|msi)
ow build installer  # binario instalador (linux) / .exe (win)
ow build uninstaller
owear-build-native  # compila native/*.cpp → .owm (usado por dev/build/plugin)
```

### Instalador (D1)

El instalador es **una app Owear aparte** (`./installer`), con su `package.json`
y opcionalmente sidecar Node. El contrato app ↔ installer es el **bridge**
`owear.bridge.ts` (`defineBridge`), que `ow build installer` embebe como
`bridge.json`. En tiempo de instalación, el kernel entra en modo installer
(`OW_MODE=installer`) y expone la API nativa `installer`:
`mode · info · bridge · payloadList · payloadRead · plan · install · uninstall ·
verify · state · shortcuts · launch · elevate`. Modos de payload: `minimal`
(binario único) y `layout` (árbol de carpetas). Ver
`docs/changelog/changelog-0.1.4.md`.

---

## 7 · Variables de entorno

| Variable | Quién la usa | Qué hace |
|---|---|---|
| `OW_APP_MAIN` | kernel | entry JS del sidecar (modo Electron-like). `ow dev`/`ow build` compilan `app/main.ts` y apuntan aquí |
| `OW_NODE_BIN` | kernel | ruta a un `node` concreto; tiene prioridad absoluta y se ignora con aviso si no sirve |
| `OW_DEV_SERVER_URL` | kernel/template | URL inicial de las ventanas en dev |
| `OW_CONTROL_SOCKET` | SDK/sidecar | ruta del socket de control |
| `OW_MODULES_DIR` | kernel | rutas `:` separadas con .owm extra |
| `OW_ASSETS_DIR` | kernel | raíz del scheme `app://` (default `./dist`) |
| `OW_DEMO` | kernel | ventana demo nativa |
| `OW_APP_NAME` / `OW_APP_ID` | kernel | identidad de la app |
| `OW_MODE` | kernel/installer | `app` \| `installer` \| `uninstaller` (lo fija el payload embebido) |
| `OW_APP_VERSION` | kernel/installer | versión de la app a instalar (de `installer.json`) |
| `OW_CLOSE_TIMEOUT_MS` | kernel | timeout del veto de cierre (default 1000) |
| `OW_KERNEL_BIN` | CLI | ruta al binario `owear` |
| `OW_MODULES_OUT` | CLI/plugin | destino de los .owm compilados |
| `OW_INCLUDE_DIR` | CLI | headers del framework para native/*.cpp |
| `XDG_RUNTIME_DIR` / `XDG_CACHE_HOME` | Linux | sockets y cache del runtime node |
