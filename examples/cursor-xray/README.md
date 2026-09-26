# 🖱️ Cursor X-Ray — Owear

Inspector de pantalla y cursor. Responde en código a la pregunta
**"¿qué hace un módulo nativo?"** y muestra cuándo usarlo.

## La respuesta corta

Un módulo nativo **expone una capacidad del sistema operativo que la web no
puede tocar**. Aquí, dos casos reales:

| Capacidad | ¿Se puede en JS/DOM? | Módulo |
|---|---|---|
| Cursor **global** de la pantalla | ❌ el DOM sólo ve el cursor dentro de la ventana | `screen` (**stock**) |
| Lista de monitores + work area + escala | ❌ | `screen` (**stock**) |
| hostname, CPUs, RAM, uptime, pid | ❌ | `hostinfo` (**propio**, `native/hostinfo.cpp`) |

O sea: **el cursor NO necesita C++ propio** — el framework ya trae el módulo
`screen`. Sólo escribes un `.owm` cuando la capacidad no está en el stock
(como `hostinfo`).

## Qué hace el ejemplo

- **▶ Seguir cursor** — polling a `ow.invoke('screen','getCursorScreenPoint')`
  cada 60 ms y dibuja el punto sobre un mapa a escala de **todos los monitores**
  (`screen.getAllDisplays()`).
- **📍 Fijar punto** — congela la posición y la marca en el mapa.
- **📋 Copiar coords** — `ow.invoke('clipboard','writeText', …)`.
- **🔔 Notificar** — `ow.invoke('notification','show', …)`.
- **🖥️ Info del sistema** — `hostinfo.info()`, un `.owm` propio compilado en
  C++ que lee el SO (uname/sysinfo en Linux, sysctl en macOS,
  GetComputerName/GlobalMemoryStatusEx en Windows).

Todas las llamadas van **directas del WebView al kernel**. No hay
`ipcRenderer.invoke` ni `ipcMain.handle` por ningún lado.

## Estructura

```
cursor-xray/
├── index.html            UI (mapa de monitores + panel lateral)
├── src/renderer.ts       usa los módulos nativos directo
├── app/main.ts           main mínimo: sólo crea la ventana
├── native/hostinfo.cpp   .owm propio (info del SO, cross-platform)
└── vite.config.ts
```

## Ejecutar

Desde la raíz del monorepo:

```bash
pnpm install
cd examples/cursor-xray
pnpm dev
```

> ⚠️ En **Wayland** la posición global del cursor puede no estar disponible
> (el compositor no la expone a las apps); `getCursorScreenPoint` usa GDK y
> puede devolver `0,0`. Bajo **X11** funciona. El módulo stock `capturer` del
> kernel tiene la misma limitación declarada.
