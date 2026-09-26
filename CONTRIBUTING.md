# Contribuir a Owear

Gracias por el interés. Este documento cubre lo que no se deduce leyendo el
código: las reglas de arquitectura que sostienen el proyecto y cómo verificar
un cambio antes de mandarlo.

## Requisitos (Linux)

Las mismas del job `linux` de CI:

```bash
sudo apt-get install -y libgtk-3-dev libwebkit2gtk-4.1-dev libsoup-3.0-dev \
  libssl-dev zlib1g-dev ninja-build xvfb libayatana-appindicator3-dev
```

Windows necesita MSVC + vcpkg (zlib/openssl) y el SDK de WebView2 en
`deps/webview2/`.

## Compilar y testear

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --test-dir build/linux-release --output-on-failure
```

Las suites E2E arrancan el kernel de verdad y le hablan por el Control Socket,
así que necesitan un display (en CI se usa Xvfb):

```bash
MODS=""
for d in build/linux-release/api/*/; do
  [ "$d" != "build/linux-release/api/CMakeFiles/" ] && MODS="$MODS$d:"
done
xvfb-run -a --server-args="-screen 0 1280x800x24" \
  env OW_APP_NAME=CI GDK_BACKEND=x11 OW_MODULES_DIR="${MODS%:}" \
  setsid ./build/linux-release/src/owear > /tmp/owear-e2e.log 2>&1 &
sleep 5
python3 tests/e2e/run_suites.py --pages all.html builtins.html veto.html --port 8123
```

En los paquetes npm: `pnpm -r typecheck` y `pnpm -r build`.

## Reglas de arquitectura

Estas cuatro reglas explican la mayoría de las decisiones del repo. Romperlas
rompe la compilación o, peor, rompe una plataforma en silencio.

1. **Anti-drift: los contratos público viven en `include/ow/`.** Los usan las
   tres plataformas y los módulos de usuario. Si cambias una firma, el cambio
   duele en compilación — que es exactamente lo que se busca.

2. **Una implementación por plataforma, elegida por CMake, nunca por `#ifdef`.**
   Los `src/**/*_linux.cpp` / `*_win.cpp` / `*.mm` se seleccionan en el
   `CMakeLists.txt` correspondiente. No metas ramas de plataforma dentro de un
   archivo común.

3. **Todo el trabajo de UI pasa por el main loop, vía `App::Post`.** Es lo que
   hace seguro que un módulo con hilo propio toque ventanas. `App::Post` es
   thread-safe; el resto de la API de ventana no lo es.

4. **El ABI de módulos (`ow_api.h`) es C y con contrato de memoria explícito.**
   Los buffers de `ow_response_t` viven sólo durante la llamada: el host copia
   al instante. Nunca lances una excepción hacia el host.

## Añadir una API

Cada carpeta `api/<nombre>/` es una API independiente con su propio target:

```
api/<nombre>/
  CMakeLists.txt          # ow_add_module(<nombre> SOURCES … LIBS …)
  src/basic.cpp           # tabla ow_fn_entry_t + ow_module_descriptor()
```

y una línea `add_subdirectory(<nombre>)` en `api/CMakeLists.txt`. Declara la
funcionalidad en la tabla del descriptor, no en un `if` de nombre de función.

Antes de “arreglar” algo que parece roto, mira si hay un test que lo cubra y
añade el que falte: el proyecto ha tenido bugs vivos detrás de una suite verde
porque el camino afectado no estaba cubierto (por ejemplo, el bridge de
`ow-window` sólo se probaba por el Control Socket, no desde el renderer).

## Escribir tests E2E

Las páginas viven en `tests/e2e/www/`. El contrato con el runner
(`tests/e2e/run_suites.py`) es:

- la página deja los resultados en `window.__R` (`{nombre: string}`) y marca
  `window.__done = 1` al terminar;
- el runner marca **FALLO** si el valor empieza por `ERR`, `timeout` o `FAIL`,
  y si la página no deja resultados. Para fallar hay que **lanzar** una
  excepción (o usar ese prefijo): resolver la promesa con un string `'ERR …'`
  no sirve, porque el `JSON.stringify` de `__R` lo deja entre comillas y no
  empieza por `ERR`;
- verifica que tu test falla sin el arreglo antes de dar el cambio por bueno.

## Roadmap y `VERIFICAR-EN-DESKTOP-REAL`

`ROADMAP.md` marca con esa etiqueta lo que CI comprueba sólo parcialmente
(diálogos modales, tray, atajos globales). Si tocas esas áreas, actualiza la
etiqueta con la versión del SO donde lo probaste — el valor está en que la
etiqueta signifique algo.
