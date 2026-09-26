# Owear

Framework de escritorio nativo multiplataforma. **Sin navegador embebido**:
usa el WebView del sistema — WebView2 (Windows), WKWebView (macOS),
WebKitGTK (Linux).

```
┌─────────────────────────────────────────────┐
│  Proceso App (nativo, ~5-10 MB)             │
│  kernel: app · windows · bridge · loader    │
│  módulos .owm: fs · dialog · los tuyos      │
└──────────────┬──────────────────────────────┘
               │ puente directo (sin Node en medio)
     WebView del SO  ←  tu frontend (Vite/React/…)
               │
     Sidecar Node (auto-instalado, opcional)
       └── app/main.ts — API tipo Electron
```

## Por qué no Electron/Tauri

| | Electron | Tauri | **Owear** |
|---|---|---|---|
| RAM del core | ~150–200 MB | ~30–60 MB | **~5–10 MB** |
| Node embebido | sí (V8+Node fijos) | no | **no — sidecar auto-instalado bajo demanda** |
| IPC | preload + pipe + V8 serialize | JSON-RPC | **host object directo + binario crudo** |
| Módulos | todos cargados | todos | **dlopen solo lo que usas (.owm)** |

## Inicio rápido

> **Estado:** los paquetes `@owear/*` todavía no se publican en npm. El runtime
> de Linux (`@owear/linux-x64-gnu`) ya se empaqueta con `tools/pack-runtime.mjs`
> (kernel + módulos stock + headers) y está listo para publicar; Windows/macOS se
> empaquetan desde runners nativos. Mientras tanto se trabaja desde un checkout
> del monorepo: `ow dev` compila el kernel la primera vez (necesita las deps del
> sistema de `.github/workflows/ci.yml`).

```bash
# desde un checkout de Owear
pnpm install
node packages/cli/src/ow.js create mi-app   # scaffoldea desde el template
```

El flujo publicado (pendiente de release) es el habitual:
`pnpm dlx @owear/cli create mi-app && cd mi-app && pnpm install && pnpm dev`.

Escribe C++ nativo junto a tu frontend:

```cpp
// native/files.cpp
#include <ow/Json.h>
#include <ow/Module.h>

static void readText(const ow_request_t* req, ow_response_t* res) {
    // args JSON → respuesta JSON. Sin excepciones hacia el host.
}

OW_MODULE_BEGIN(files, "1.0.0")
OW_FN(readText)
OW_MODULE_END()
```

Y llámalo desde el renderer con tipos generados:

```ts
import { files } from '@owear/native'
const txt = await files.readText('/etc/hostname')
```

O maneja la app desde el main process estilo Electron:

```ts
// app/main.ts (sidecar Node)
import { app, BrowserWindow } from '@owear/core'

app.whenReady().then(() => {
  new BrowserWindow({ width: 1200, height: 800, titleBarStyle: 'custom' })
})
```

## Build del framework (desde este repo)

```bash
cmake --preset linux-release        # windows-release / macos-release
cmake --build --preset linux-release
ctest --test-dir build/linux-release --output-on-failure
```

## Estructura

- `include/ow/` — contratos públicos (cambiarlos rompe las 3 plataformas: anti-drift)
- `src/<Módulo>/<archivo>_<plat>.cpp` — una implementación por plataforma, seleccionada por CMake
- `api/<nombre>/` — **una carpeta por API** con su manifiesto `owear.module.json`
  (fuente única de verdad). Ver [APIs](docs/APIS.md)
- `tools/gen-apis.mjs` — genera el descubrimiento (CMake) y el registro de builtins desde los manifiestos
- `packages/` — SDK npm (`@owear/core`, `@owear/cli`, `@owear/vite-plugin`)
- `docs/` — [Roadmap](docs/ROADMAP.md) · [Protocolo](docs/BRIDGE.md) · [API](docs/API.md) · [APIs](docs/APIS.md)

## Contribuir

Reglas de arquitectura, cómo añadir una API y cómo escribir tests E2E:
[CONTRIBUTING.md](CONTRIBUTING.md).

## Licencia

Apache License 2.0 — ver [LICENSE](LICENSE).
