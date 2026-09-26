<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Owear Starter

Starter **profesional** de Owear: UI real, titlebar propia y llamadas a los
módulos nativos del kernel desde el renderer.

## Ejecutar

Desde la raíz del monorepo:

```bash
pnpm install
cd examples/starter
pnpm dev
```

## Qué incluye

- **Titlebar propia** (drag + minimizar/maximizar/cerrar) vía el builtin
  `ow-window`.
- **Hero + tarjetas**: Entorno (plataforma/motor/escala) y Consola de salida.
- **Demos de módulos nativos**, sin `ipcRenderer`/`ipcMain`:

  | Botón | Módulos |
  |---|---|
  | Abrir archivo… | `dialog.open` → `fs.stat` + `fs.readText` |
  | Notificar | `notification.show` |
  | Copiar ruta | `clipboard.writeText` |
  | Mostrar en carpeta | `shell.showItemInFolder` |
  | Acerca de | `dialog.messageBox` |

## Estructura

```
├── index.html        titlebar, hero, tarjetas, consola
├── src/
│   ├── renderer.ts   lógica + llamadas nativas
│   ├── style.css     tema oscuro
│   └── ow.d.ts       tipos de window.ow
└── app/
    └── main.ts       main (sidecar): sólo la ventana
```

> Este ejemplo **no necesita compilador C++**: usa sólo módulos stock. Para ver
> cómo añadir un `.owm` propio, mira [`../cursor-xray`](../cursor-xray).
