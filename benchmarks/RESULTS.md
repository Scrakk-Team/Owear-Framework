<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Benchmarks — Owear vs Electron vs Tauri

Media de **2 repetición(es)**, misma máquina (Linux, Xvfb 1280×800, 4 vCPU).
Reproducir: `python3 benchmarks/run.py --repeat 3`. Detalle crudo: `results.json`.

> Electron embebe **Chromium**; Owear/Tauri usan el **WebView del SO** (WebKitGTK).
> Electron corre `--no-sandbox --disable-gpu` (contenedor sin GPU). Owear "sin main" no arranca Node.

## Arranque, memoria y tamaño

| Framework | Arranque (ms) | RAM idle (MB) | RAM pico (MB) | Procesos | Tamaño (MB) |
|---|---:|---:|---:|---:|---:|
| Owear (con main) | 2,180 | 452.5 | 672.1 | 5 | 6.1 |
| Owear (sin main) | 1,862 | 396.4 | 643.8 | 4 | 6.1 |
| Electron | 1,310 | 634.2 | 801.9 | 8 | 261.9 |
| Tauri | 1,156 | 402.4 | 543 | 4 | 11.6 |

## IPC — round-trip pequeño (secuencial)

| Framework | ops/s | mediana (ms) | p95 (ms) | p99 (ms) |
|---|---:|---:|---:|---:|
| Owear (con main) | 1,129 | 1 | 2 | 3.5 |
| Owear (sin main) | 1,258 | 1 | 2 | 3 |
| Electron | 3,071 | 0.3 | 0.7 | 1.6 |
| Tauri | 934 | 1 | 2.5 | 4 |

## IPC — concurrente (2000 en lotes de 50)

| Framework | ops/s |
|---|---:|
| Owear (con main) | 3,028 |
| Owear (sin main) | 3,845 |
| Electron | 8,915 |
| Tauri | 2,729 |

## Payload — eco de texto ida y vuelta (mediana ms)

| Framework | 1 KB | 64 KB | 1 MB | 5 MB |
|---|---:|---:|---:|---:|
| Owear (con main) | 1.5 | 3.5 | 38.5 | 243.5 |
| Owear (sin main) | 1 | 2.5 | 26 | 135.5 |
| Electron | 0.3 | 1.9 | 13.5 | 53.4 |
| Tauri | 1 | 2.5 | 20.5 | 104.5 |

## Binario — leer 5 MB nativo→renderer

| Framework | mediana (ms) | MB/s |
|---|---:|---:|
| Owear (con main) | 86 | 58.9 |
| Owear (sin main) | 62 | 81.6 |
| Electron | 43 | 115.3 |
| Tauri | 53 | 94.4 |

## Descarga — respuesta de 1 MB / 5 MB (mediana ms)

| Framework | 1 MB | 5 MB |
|---|---:|---:|
| Owear (con main) | 17 | 131 |
| Owear (sin main) | 16 | 82 |
| Electron | 7 | 34 |
| Tauri | 16 | 92 |

## Eventos nativo → renderer (5000)

| Framework | ms | eventos/s |
|---|---:|---:|
| Owear (con main) | 36 | 142,857 |
| Owear (sin main) | 17 | 294,118 |
| Electron | 139 | 35,950 |
| Tauri | 4,946 | 1,012 |

## Cómputo JS (motor)

| Framework | loop 20M (ms) | sort 2M (ms) |
|---|---:|---:|
| Owear (con main) | 44 | 1,684 |
| Owear (sin main) | 24 | 1,010 |
| Electron | 26 | 2,469 |
| Tauri | 25 | 1,094 |

---
Generado por `benchmarks/make_tables.py`.
