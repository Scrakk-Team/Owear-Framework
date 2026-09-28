<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Benchmarks — Owear vs Electron vs Tauri

**Mediana de 3 repeticiones intercaladas** (round-robin), misma máquina (Linux, Xvfb 1280×800, 4 vCPU).
Reproducir: `python3 benchmarks/run.py --repeat 3`. Detalle crudo: `results.json`.

> Electron embebe **Chromium**; Owear/Tauri usan el **WebView del SO** (WebKitGTK).
> Electron corre `--no-sandbox --disable-gpu` (contenedor sin GPU). Owear "sin main" no arranca Node.

## Arranque, memoria y tamaño

| Framework | Arranque (ms) | RAM idle (MB) | RAM pico (MB) | Procesos | Tamaño (MB) |
|---|---:|---:|---:|---:|---:|
| Owear (con main) | 2,429 | 425.2 | 654.3 | 5 | 6.2 |
| Owear (sin main) | 1,610 | 365.7 | 599.8 | 4 | 6.2 |
| Electron | 1,208 | 634.5 | 786.6 | 8 | 261.9 |
| Tauri | 1,407 | 403.5 | 545.7 | 4 | 11.6 |

## IPC — round-trip pequeño (secuencial)

| Framework | ops/s | mediana (ms) | p95 (ms) | p99 (ms) |
|---|---:|---:|---:|---:|
| Owear (con main) | 835 | 1 | 3 | 5 |
| Owear (sin main) | 1,363 | 1 | 2 | 4 |
| Electron | 2,939 | 0.3 | 0.7 | 1.5 |
| Tauri | 1,029 | 1 | 2 | 3 |

## IPC — concurrente (2000 en lotes de 50)

| Framework | ops/s |
|---|---:|
| Owear (con main) | 2,424 |
| Owear (sin main) | 3,215 |
| Electron | 9,412 |
| Tauri | 2,571 |

## Payload — eco de texto ida y vuelta (mediana ms)

| Framework | 1 KB | 64 KB | 1 MB | 5 MB |
|---|---:|---:|---:|---:|
| Owear (con main) | 1 | 3 | 32 | 169 |
| Owear (sin main) | 1 | 3 | 26 | 154 |
| Electron | 0.7 | 2.7 | 13.9 | 55.9 |
| Tauri | 1 | 3 | 22 | 106 |

## Binario — leer 5 MB nativo→renderer

| Framework | mediana (ms) | MB/s |
|---|---:|---:|
| Owear (con main) | 74 | 67.6 |
| Owear (sin main) | 62 | 80.6 |
| Electron | 45 | 110.6 |
| Tauri | 50 | 100 |

## Descarga — respuesta de 1 MB / 5 MB (mediana ms)

| Framework | 1 MB | 5 MB |
|---|---:|---:|
| Owear (con main) | 17 | 72 |
| Owear (sin main) | 18 | 87 |
| Electron | 7 | 34 |
| Tauri | 11 | 63 |

## Eventos nativo → renderer (5000)

| Framework | ms | eventos/s |
|---|---:|---:|
| Owear (con main) | 30 | 166,667 |
| Owear (sin main) | 20 | 250,000 |
| Electron | 148 | 33,784 |
| Tauri | 4,681 | 1,068 |

## Cómputo JS (motor)

| Framework | loop 20M (ms) | sort 2M (ms) |
|---|---:|---:|
| Owear (con main) | 33 | 983 |
| Owear (sin main) | 24 | 991 |
| Electron | 26 | 4,410 |
| Tauri | 22 | 1,009 |

---
Generado por `benchmarks/make_tables.py`.
