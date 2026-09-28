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
| Owear (con main) | 2,112 | 451.6 | 742.5 | 5 | 6.1 |
| Owear (sin main) | 1,913 | 395.6 | 658.3 | 4 | 6.1 |
| Electron | 1,612 | 637 | 786.4 | 8 | 261.9 |
| Tauri | 1,259 | 402.2 | 539.9 | 4 | 11.6 |

## IPC — round-trip pequeño (secuencial)

| Framework | ops/s | mediana (ms) | p95 (ms) | p99 (ms) |
|---|---:|---:|---:|---:|
| Owear (con main) | 670 | 1 | 3.5 | 5 |
| Owear (sin main) | 611 | 1 | 4 | 5.5 |
| Electron | 1,364 | 0.5 | 2.1 | 4 |
| Tauri | 580 | 1 | 4 | 6.5 |

## IPC — concurrente (2000 en lotes de 50)

| Framework | ops/s |
|---|---:|
| Owear (con main) | 798 |
| Owear (sin main) | 807 |
| Electron | 6,459 |
| Tauri | 1,901 |

## Payload — eco de texto ida y vuelta (mediana ms)

| Framework | 1 KB | 64 KB | 1 MB | 5 MB |
|---|---:|---:|---:|---:|
| Owear (con main) | 1.5 | 7.5 | 46 | 199.5 |
| Owear (sin main) | 1.5 | 7.5 | 45 | 212 |
| Electron | 0.6 | 2.1 | 18.9 | 74.2 |
| Tauri | 2.5 | 3 | 30 | 174 |

## Binario — leer 5 MB nativo→renderer

| Framework | mediana (ms) | MB/s |
|---|---:|---:|
| Owear (con main) | 66 | 75.8 |
| Owear (sin main) | 75 | 66.8 |
| Electron | 57 | 87.1 |
| Tauri | 64 | 79.1 |

## Descarga — respuesta de 1 MB / 5 MB (mediana ms)

| Framework | 1 MB | 5 MB |
|---|---:|---:|
| Owear (con main) | 28 | 100 |
| Owear (sin main) | 24 | 103 |
| Electron | 10 | 41 |
| Tauri | 22 | 79 |

## Eventos nativo → renderer (5000)

| Framework | ms | eventos/s |
|---|---:|---:|
| Owear (con main) | 26 | 192,029 |
| Owear (sin main) | 22 | 232,684 |
| Electron | 223 | 23,103 |
| Tauri | 4,179 | 1,326 |

## Cómputo JS (motor)

| Framework | loop 20M (ms) | sort 2M (ms) |
|---|---:|---:|
| Owear (con main) | 26 | 1,178 |
| Owear (sin main) | 29 | 1,184 |
| Electron | 30 | 3,505 |
| Tauri | 28 | 1,307 |

---
Generado por `benchmarks/make_tables.py`.
