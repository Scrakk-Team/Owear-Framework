# APIs de Owear — manifiestos y registro

Toda API vive en su carpeta `api/<nombre>/` y se declara en un **manifiesto**
`owear.module.json`. Ese manifiesto es la **fuente única de verdad**: de él se
derivan el descubrimiento en CMake, el registro de builtins y la validación de
CI. Añadir una API **no** requiere editar listas a mano.

## Estructura

```
api/<nombre>/
├── owear.module.json     ← manifiesto (fuente única de verdad)
├── CMakeLists.txt        ← receta de compilación de la API (módulos)
├── README.md
└── src/
    ├── <nombre>.cpp              (cross-platform)
    ├── <nombre>_linux.cpp        (por plataforma)
    ├── <nombre>_win.cpp
    └── <nombre>_mac.mm
```

## Dos tipos: `module` y `builtin`

| kind | Qué es | Cómo se carga |
|---|---|---|
| `module` | Compila a `<nombre>.so/.dll/.dylib` (`.owm`) independiente. | `dlopen` en runtime desde `OW_MODULES_DIR` / `<exe>/modules`. |
| `builtin` | Acoplado al kernel (necesita `LiveWindow`, `ControlServer`, el WebView). Su fuente se enlaza dentro del binario. | `RegisterGeneratedBuiltins()` (generado) lo registra en el `Dispatcher`. |

## Manifiesto

### Módulo

```json
{
  "$schema": "../owear.module.schema.json",
  "name": "screen",
  "kind": "module",
  "version": "0.1.0",
  "description": "Monitores, display primario y posición global del cursor.",
  "platforms": ["linux", "win", "mac"],
  "optional": false,
  "functions": ["getAllDisplays", "getPrimaryDisplay", "getCursorScreenPoint"]
}
```

### Builtin

```json
{
  "name": "window",
  "kind": "builtin",
  "version": "0.1.0",
  "descriptors": [
    { "name": "ow-window", "factory": "WindowModuleDescriptorImpl", "core": true,
      "platforms": ["linux", "win", "mac"], "functions": ["minimize", "..."] },
    { "name": "window", "factory": "WindowExtrasDescriptor",
      "platforms": ["linux"], "functions": ["capturePage", "..."] }
  ],
  "sources": { "linux": ["src/window_extra.cpp"], "win": ["src/window_extra_win.cpp"] }
}
```

- `factory`: función C++ que devuelve el `ow_module_desc_t*`.
- `core: true`: la fuente vive en `src/Core/…` (no se enlaza desde `api/`).
- `optional: true`: la API puede omitirse si faltan deps del sistema.

## Generación

```bash
node tools/gen-apis.mjs           # regenera
node tools/gen-apis.mjs --check   # falla si está desactualizado (CI)
node tools/check-apis.mjs         # manifiesto ↔ código C++
```

Genera (no editar a mano):

| Fichero | Contenido |
|---|---|
| `api/generated.cmake` | `add_subdirectory(<nombre>)` de cada `module`. Lo incluye `api/CMakeLists.txt`. |
| `src/Core/builtins.generated.cmake` | fuentes de builtins por plataforma (`OW_BUILTIN_SOURCES_<plat>`). Lo incluye `src/CMakeLists.txt`. |
| `src/Core/BuiltinRegistry.generated.cpp` | `RegisterGeneratedBuiltins()`: registra cada descriptor builtin en el `Dispatcher`. |

`tools/check-apis.mjs` valida que el descriptor C++ se llame como el manifiesto
y que las funciones coincidan **exactamente** (incluidos los lambda de tabla).

## Registro en runtime

El kernel mantiene un **registry** (`src/Bridge/Dispatcher.cpp`):

- `RegisterModule(desc, origin)` guarda `{name, version, origin, builtin, functions}`.
- Módulos dinámicos se descubren con `ModuleLoader::LoadAll()` (escaneo de
  directorios + `dlopen` + `ow_module_descriptor`).
- El renderer/Node lo consultan por el **Control Socket**:
  - `module.list` → `[{name, version, origin, builtin, functions, functionNames}]`
  - `module.info {name}` → el mismo objeto de un módulo, o error.

Desde `@owear/core`: `listNativeModules()` y `nativeModuleInfo(name)`.

## Añadir una API

```bash
ow api new mi-api     # crea api/mi-api/ + manifiesto + esqueleto + regenera
# edita api/mi-api/src/mi-api.cpp (y el manifiesto si añades funciones)
cmake --build --preset linux-release
```

En el renderer: `await ow.invoke('mi-api', 'ping')`.
