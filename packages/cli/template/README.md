# __APP_NAME__

App de escritorio construida con **[Owear](https://owear.dev)**: el WebView del
sistema renderiza tu frontend web y el kernel nativo te da el sistema operativo.

## Empezar

```bash
npm install
npm run dev     # ≡ ow dev
```

Se abre una ventana con el WebView del sistema. Edita `src/renderer.ts` y el
hot-reload de Vite lo refleja al instante.

```bash
npm run build   # ≡ ow build → dist/
```

## Estructura

```
├── index.html          UI (titlebar propia, hero, tarjetas)
├── src/
│   ├── renderer.ts     lógica del renderer + llamadas a módulos nativos
│   ├── style.css       estilos
│   └── ow.d.ts         tipos de window.ow
└── app/
    └── main.ts         proceso principal (sidecar Node): sólo la ventana
```

## Cómo se llama al sistema

Sin `ipcRenderer` ni `ipcMain`: el renderer invoca los módulos nativos del
kernel directo.

```ts
const path = await ow.invoke('dialog', 'open', 'open', 'Abrir archivo')
const text = await ow.invoke('fs', 'readText', path)
await ow.invoke('notification', 'show', 'Listo', `Leídos ${text.length} bytes`)
```

Módulos incluidos (stock): `fs` · `path` · `process` (PTY) · `dialog` ·
`clipboard` · `shell` · `screen` · `net` · `notification` · `power` · `menu` ·
`globalshortcut` · `updater` · `capturer`, más los builtins `ow-window` (ventana),
`window` (extras del WebView), `session`, `app` y `crashreporter`.

## Añadir un módulo nativo propio (C++)

1. Crea `native/mi_modulo.cpp`:

   ```cpp
   #include <ow/Json.h>
   #include <ow/Module.h>
   #include "ow_api.h"

   static void ping(const ow_request_t*, ow_response_t* res) {
       ow::Module::RespondOk(res, "\"pong\"");
   }

   OW_MODULE_BEGIN(mi_modulo, "1.0.0")
   OW_FN(ping)
   OW_MODULE_END()
   ```

2. `ow dev` lo compila a `.owm` y genera el binding. Úsalo en el renderer:

   ```ts
   import { mi_modulo } from '@owear/native'
   await mi_modulo.ping()
   ```

> Requiere un compilador de C++ en el sistema. Si no lo tienes, no añadas
> `native/` y la app sigue funcionando con los módulos stock.
