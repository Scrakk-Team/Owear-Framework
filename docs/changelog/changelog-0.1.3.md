<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.3

Arreglo de la ventana de Windows (en blanco / "no responde") y toolchain de
compilación cruzada Linux → Windows.

## Added

- **Cross-compilación Linux → Windows**: `clang-cl` + `lld-link` con el
  MSVC/Windows SDK descargado por `xwin` (`cmake/windows-cross.cmake`). Permite
  generar `owear.exe` + módulos desde Linux, sin Visual Studio ni CI.
- `tools/windows/dev-*.ps1`: bucle de desarrollo local en Windows sin publicar
  paquetes a npm.

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
- Diagnóstico del ciclo WebView2: `environment → controller → navigate`
  (URL, bounds, `nav completed`, `ProcessFailed`) y `put_IsVisible(TRUE)`.
