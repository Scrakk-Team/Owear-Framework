# PicGo · port a Owear

Port de [PicGo](https://github.com/Molunerfinn/PicGo) a **Owear**: un gestor de
subida de imágenes. Reutiliza el **core de PicGo** (`picgo` en npm) para el motor
de subida; la UI es HTML/CSS/JS sobre Owear (sin Electron).

## Cómo funciona

- **Sidecar Node** (`app/main.ts`): instancia el core `picgo` y lo expone al
  renderer con `app.handle('picgo.upload', …)`. Usa la misma config que el PicGo
  de escritorio: `~/.picgo/config.json`.
- **Renderer** (`src/renderer.ts`): elige/arrastra imágenes, sube y copia la URL.
  Llama a Node con `ow.invoke('node','call',{ fn, args })`.

## Uso

```bash
cd examples/picgo
pnpm install        # instala @owear/core, picgo, vite…
# asegúrate de tener un "picbed" configurado en ~/.picgo/config.json
#   (o usa el PicGo de escritorio una vez para generarlo)
pnpm dev            # ow dev: kernel + vite + sidecar node
```

`pnpm build` (≡ `ow build`) genera `dist/`; `ow build app --format deb|appimage`
(o `--format binary`) produce un instalable.

## Estado

Primera pasada funcional del flujo principal (elegir → subir → copiar URL) sobre
el core de PicGo. Pendiente respecto al PicGo completo: historial/galería,
gestión de plugins, editor de config in-app y el resto de la UI. Se porta sobre
el core, así que los *picbeds* y plugins de PicGo siguen siendo compatibles.
