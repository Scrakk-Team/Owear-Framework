<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.2

Release de distribución multiplataforma, con el runtime de Windows y el arreglo
de la ventana en blanco.

## Added

- **Runtime de Windows `@owear/win32-x64`**: kernel + 15 módulos stock (`.dll`)
  + headers, **autocontenido** (incluye `zlib`, OpenSSL y el runtime MSVC
  `VCRUNTIME`/`MSVCP`, así que no hace falta VC++ Redistributable).
- `@owear/win32-x64` añadido como **dependencia opcional** de `@owear/cli`
  (se instala solo en Windows x64).
- Workflow `Build runtime packages` que compila los runtimes por plataforma en
  runners nativos y los publica como artifacts.
- `tools/windows/setup.ps1` + `docs/windows.md`: instalación mínima en Windows.

## Fixed

- **Ventana en blanco / "no responde" en Windows**: el controller de WebView2
  **no se redimensionaba** en `WM_SIZE` (quedaba con bounds inválidos). Ahora se
  llama a `Resize` en cada `WM_SIZE`.
- `WM_NCCALCSIZE` con titlebar custom ahora aplica los **insets del marco**
  (técnica Electron) en vez de devolver `0` a secas, que rompía el hit-testing
  del borde en Windows 10.
- **DPI awareness** (`PER_MONITOR_AWARE_V2` antes de crear ventanas).
- **Módulos cargados dos veces**: `ModuleLoader::SearchPaths` deduplica
  `OW_MODULES_DIR` vs `<exe>/modules`.
- Empaquetado de runtime: copia **todas** las DLL de dependencia (`z.dll`,
  `libssl`/`libcrypto`, runtime MSVC) **junto al `owear.exe`**.
- Soporte del layout **multi-config de MSVC** (`src/Release/owear.exe`,
  `api/<x>/Release/<x>.dll`) en `tools/pack-runtime.mjs`.
- Diagnóstico del ciclo WebView2: `environment → controller → navigate`
  (URL, bounds, `nav completed`, `ProcessFailed`) y `put_IsVisible(TRUE)`.
