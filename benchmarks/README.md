<!-- Copyright 2026 Owear Contributors — SPDX-License-Identifier: Apache-2.0 -->

# Benchmarks — Owear vs Electron vs Tauri vs Neutralino

Comparativa reproducible de rendimiento **en Linux**. Cada framework corre **la
misma app** (mismo HTML/JS, `web/bench.js`) contra un backend propio, con las
mismas condiciones (misma ventana 900×700, GPU software, Xvfb 1280×800).

## Cómo correr

```bash
# warmup + 3 repeticiones intercaladas (round-robin) de todos los frameworks
python3 benchmarks/run.py --warmup 1 --repeat 3

# solo uno, con repeticiones
python3 benchmarks/run.py --only neutralino --repeat 3

# cold start (vacía cachés de disco antes de cada corrida; requiere sudo)
BENCH_SUDO_PW='…' python3 benchmarks/run.py --warmup 1 --repeat 3 --cold

# volcar crudo y regenerar el informe + gráficos
python3 benchmarks/raw.py
python3 benchmarks/make_tables.py        # → RESULTS.md + charts/*.svg
```

Salidas: `results.json` (crudo + `meta`), `RESULTS.md` (tablas + % + gráficos),
`charts/*.svg`.

## Zonas medidas

| Zona | Cómo |
|---|---|
| **Arranque** | spawn → marcador `<fw>.ready` (la app lo escribe al estar la página lista), muestreo a 5 ms |
| **Memoria** | **PSS** (`smaps_rollup`) del árbol cada 20 ms: idle, pico (+ RSS crudo de referencia) |
| **Procesos** | nº de procesos del árbol en idle |
| **Tamaño** | footprint para ejecutar (kernel + módulos / dist de Electron / binario Tauri / bin+resources de Neutralino) |
| **IPC** | eco secuencial (2000, mediana/p95/p99), concurrente (2000 en lotes de 50) |
| **Payload** | eco de texto 1 KB / 64 KB / 1 MB / 5 MB |
| **Fichero** | leer 5 MB nativo→renderer (SHM/copia) |
| **Descarga** | respuesta nativa de 1 MB / 5 MB |
| **Eventos** | 5000 eventos nativo→renderer |
| **JS (motor)** | loop 20 M + sort 2 M — **depende del motor** (V8 vs JSC), no del framework |

## Metodología

- **Xvfb compartido** (uno solo, `DISPLAY=:97`): quita el coste variable de
  arrancar X por corrida.
- **Arranque por marcador**, no por parseo de log: cada adaptador escribe
  `out/<fw>.ready` al quedar la página lista; el runner lo sondea cada 5 ms.
- **PSS en vez de RSS**: Electron reparte memoria entre procesos; con RSS se
  contaría varias veces. Se guarda también el RSS crudo como referencia.
- **N repeticiones intercaladas** (round-robin) + **warmup** descartado → reparte
  el sesgo de orden y de calentamiento (JIT/cachés).
- **Metadata** (`meta` en `results.json`): CPU, cores, governor, kernel, SO,
  versiones (Electron, WebKitGTK, Neutralino, Owear) y flags usados.

## Porcentajes y score

`make_tables.py` normaliza cada métrica a **"menos es mejor"** (invierte las de
throughput), usa **Electron = 100 %** y calcula el **% de cada framework** por
zona. El **score neto** de Owear es la **media geométrica** de sus ratios vs
Electron en todas las zonas (< 100 % = mejor en conjunto).

## Justicia (caveats)

- **Renderer**: Electron embebe **Chromium**; Owear/Tauri/Neutralino usan el
  **WebView del sistema** (WebKitGTK en Linux). Las métricas de **motor JS** y
  parte del render dependen del webview, **no** del framework.
- **GPU**: todo corre en **software** (Xvfb). Electron `--no-sandbox --disable-gpu`;
  Owear `OW_GPU=off`.
- **Neutralino no tiene IPC nativo genérico sin extensión**: se usa una extensión
  propia (`neutralino/ext/bench.js`, Node) → su "invoke" es
  renderer → server → extensión → server → renderer (más saltos).
- **Owear "sin main"** = sin sidecar Node (llamadas directas al módulo nativo).
- El **tamaño** cuenta solo lo que cada framework aporta; Tauri/Owear/Neutralino
  no incluyen la WebView del sistema (no la empaquetan), Electron sí.

## Layout

```
web/bench.js         lógica compartida (fases del bench)
owear/               kernel + módulo nativo bench.cpp + assets/adapter.js
electron/            main.js + preload.js + adapter.js
tauri/               src-tauri (Rust) + dist/ (UI) + adapter.js
neutralino/          config + ext/bench.js (extensión) + resources/adapter.js
run.py               runner (arranque/PSS/metadata)   raw.py  make_tables.py
results.json         crudo + meta                     RESULTS.md  charts/*.svg
```
