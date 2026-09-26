<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.3

Arreglos del backend de Windows (ventana blanca / "no responde").

## Fixed

- **WebView2 no se redimensionaba en `WM_SIZE`**: el controller quedaba con
  bounds inválidos y la ventana salía en blanco. Ahora se llama a `Resize` en
  cada `WM_SIZE`.
- **`WM_NCCALCSIZE`** con titlebar custom ahora aplica los insets del marco
  (técnica Electron/ole) en vez de devolver `0` a secas, rompiendo el
  hit-testing del borde en Windows 10.
- **DPI awareness** (`PER_MONITOR_AWARE_V2` antes de crear ventanas).
- **Módulos cargados dos veces**: `ModuleLoader::SearchPaths` deduplica
  `OW_MODULES_DIR` vs `<exe>/modules`.

## Added

- Diagnóstico del ciclo WebView2: `environment → controller → navigate`
  (URL, bounds, `nav completed`, y `ProcessFailed` para detectar caídas del
  renderer/GPU).
- `put_IsVisible(TRUE)` explícito al navegar.
