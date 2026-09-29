<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Instalador de Owear (`ow create installer`)

Este proyecto es una **app Owear aparte** que instala tu app. Se compila con
`ow build installer` a un **binario instalador** (Linux: binario; Windows: `.exe`)
que lleva embebido el payload de la app y el **bridge** (`owear.bridge.ts`).

## Estructura

```
installer/
  index.html            # UI del instalador (vanilla por defecto)
  src/installer.ts      # lógica de la UI (usa la API nativa `installer`)
  app/main.ts           # sidecar Node (crea la ventana del instalador)
  vite.config.ts        # outDir: ui-dist  (ow build installer lo recoge como ui/)
  uninstaller/          # OTRO template: mismo bridge, pero desinstala
```

## Cómo funciona

1. `ow build installer` compila el bridge (`owear.bridge.ts`), la app (payload),
   esta UI y (si existe) el desinstalador.
2. Se ensambla el payload del instalador (`ui/`, `payload/`, `bridge.json`,
   `installer.json`) y se empaqueta dentro del kernel (footer `OWPK1`).
3. Al ejecutar el instalador, el kernel detecta `installer.json`, entra en
   **modo installer** (`OW_MODE=installer`), sirve `ui/` como assets y expone la
   API nativa `installer`.
4. La UI (`src/installer.ts`) usa esa API para planificar y desplegar la app.

## API disponible (renderer y main)

`installer.info / bridge / payloadList / payloadRead / plan / install /
uninstall / verify / state / shortcuts / launch / elevate / mode`.

Puedes personalizar la UI como quieras (React, Vue, Tailwind…): sigue siendo
una app Owear normal.
