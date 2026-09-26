<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.2

Release de distribución multiplataforma y arreglo del runtime de Windows.

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

- El empaquetado de runtime ahora copia **todas** las DLL de dependencia
  (`z.dll`, `libssl`/`libcrypto`, runtime MSVC) **junto al `owear.exe`**, no en
  `modules/`, para que Windows las encuentre al cargar.
- Soporte del layout **multi-config de MSVC** (`src/Release/owear.exe`,
  `api/<x>/Release/<x>.dll`) en `tools/pack-runtime.mjs`.
