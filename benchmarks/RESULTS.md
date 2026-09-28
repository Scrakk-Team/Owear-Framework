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
| Owear (con main) | 2,171 | 459.4 | 672.6 | 5 | 6.1 |
| Owear (sin main) | 2,016 | 396.3 | 609.8 | 4 | 6.1 |
| Electron | 1,918 | 634.6 | 802.7 | 8 | 261.9 |
| Tauri | 1,411 | 402.1 | 539.2 | 4 | 11.6 |

## IPC — round-trip pequeño (secuencial)

| Framework | ops/s | mediana (ms) | p95 (ms) | p99 (ms) |
|---|---:|---:|---:|---:|
| Owear (con main) | 621 | 1 | 4 | 7 |
| Owear (sin main) | 590 | 1 | 4.5 | 6.5 |
| Electron | 1,210 | 0.55 | 2.5 | 4.2 |
| Tauri | 503 | 2 | 5 | 7 |

## IPC — concurrente (2000 en lotes de 50)

| Framework | ops/s |
|---|---:|
| Owear (con main) | 2,192 |
| Owear (sin main) | 2,168 |
| Electron | 5,929 |
| Tauri | 1,537 |

## Payload — eco de texto ida y vuelta (mediana ms)

| Framework | 1 KB | 64 KB | 1 MB | 5 MB |
|---|---:|---:|---:|---:|
| Owear (con main) | 3 | 5.5 | 37 | 209 |
| Owear (sin main) | 3 | 5.5 | 39 | 216 |
| Electron | 0.8 | 3.4 | 19.6 | 76.5 |
| Tauri | 3 | 4 | 30.5 | 178.5 |

## Binario — leer 5 MB nativo→renderer

| Framework | mediana (ms) | MB/s |
|---|---:|---:|
| Owear (con main) | 84 | 59.6 |
| Owear (sin main) | 92 | 54.4 |
| Electron | 66 | 75.8 |
| Tauri | 74 | 68 |

## Descarga — respuesta de 1 MB / 5 MB (mediana ms)

| Framework | 1 MB | 5 MB |
|---|---:|---:|
| Owear (con main) | 29 | 114 |
| Owear (sin main) | 20 | 136 |
| Electron | 12 | 44 |
| Tauri | 28 | 120 |

## Eventos nativo → renderer (5000)

| Framework | ms | eventos/s |
|---|---:|---:|
| Owear (con main) | 32 | 160,714 |
| Owear (sin main) | 38 | 140,555 |
| Electron | 199 | 25,210 |
| Tauri | 3,065 | 1,641 |

## Cómputo JS (motor)

| Framework | loop 20M (ms) | sort 2M (ms) |
|---|---:|---:|
| Owear (con main) | 42 | 1,330 |
| Owear (sin main) | 28 | 1,332 |
| Electron | 31 | 3,520 |
| Tauri | 39 | 1,363 |

---
Generado por `benchmarks/make_tables.py`.
