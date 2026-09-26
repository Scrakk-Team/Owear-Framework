<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.1

Primera release distribuida por npm, con el registro de APIs modular y el
runtime de Linux como paquete.

## Added

- **Manifiestos por API** (`api/<nombre>/owear.module.json`) como fuente única de
  verdad: descubrimiento dinámico en CMake y registro de builtins generados
  (`tools/gen-apis.mjs`). Añadir una API ya no requiere editar listas a mano.
- **Registry con metadata**: `module.list` / `module.info` (versión, origen,
  builtin, funciones) y `listNativeModules()` / `nativeModuleInfo()` en el SDK.
- **Paquete de runtime `@owear/linux-x64-gnu`**: kernel nativo + módulos stock
  (`.owm`) + headers, listo para npm (`tools/pack-runtime.mjs`).
- **Starter profesional** en `ow create`: titlebar propia, demos de módulos
  nativos y sustitución del nombre de la app.
- **CLI**: `ow api list` y `ow api new`.
- **Ejemplos**: `examples/starter`, `examples/cursor-xray`, `examples/snake`.
- **Tooling**: `ow api` y checks de CI (`gen-apis --check`, `check-apis`,
  `license-header --check`, `version check`).

## Changed

- **Ventanas en Linux**: esquinas redondeadas y **resize nativo por borde** en
  ventanas sin decoración (filtro GDK + ventana RGBA + WebView transparente).
- **Resolución del runtime Node en cascada**: `OW_NODE_BIN` → Node del sistema →
  caché → descarga, registrando la procedencia (`env|system|cache|downloaded`).
- El timeout de cierre (`closeRequested`) es configurable vía
  `OW_CLOSE_TIMEOUT_MS` (default 1000 ms).
- Licencia **Apache-2.0** con cabecera SPDX en todo el código.

## Fixed

- `ow dev` ahora **carga los módulos stock** y, dentro del monorepo, usa el
  kernel recién compilado en vez del paquete npm.
- Resolución de `esbuild` para compilar `app/main.ts` (dependencia directa).
- Fiabilidad del **control socket en Windows** (pipe, `app.quit`,
  `ImpersonateNamedPipeClient`) y del **E2E en macOS/Linux**.
