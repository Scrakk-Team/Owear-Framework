# Benchmarks — Owear vs Electron vs Tauri

Media de **2 repetición(es)**, misma máquina (Linux, Xvfb 1280×800, 4 vCPU).
Reproducir: `python3 benchmarks/run.py --repeat 3`. Detalle crudo: `results.json`.

> Electron embebe **Chromium**; Owear/Tauri usan el **WebView del SO** (WebKitGTK).
> Electron corre `--no-sandbox --disable-gpu` (contenedor sin GPU). Owear "sin main" no arranca Node.

## Arranque, memoria y tamaño

| Framework | Arranque (ms) | RAM idle (MB) | RAM pico (MB) | Procesos | Tamaño (MB) |
|---|---:|---:|---:|---:|---:|
| Owear (con main) | 2,225 | 454.9 | 668.1 | 5 | 6.1 |
| Owear (sin main) | 1,911 | 394.4 | 608.3 | 4 | 6.1 |
| Electron | 1,614 | 634.4 | 776.1 | 8 | 261.9 |
| Tauri | 1,260 | 402.9 | 544 | 4 | 11.6 |

## IPC — round-trip pequeño (secuencial)

| Framework | ops/s | mediana (ms) | p95 (ms) | p99 (ms) |
|---|---:|---:|---:|---:|
| Owear (con main) | 591 | 1.5 | 4.5 | 6.5 |
| Owear (sin main) | 658 | 1 | 4 | 5.5 |
| Electron | 1,383 | 0.45 | 2.1 | 3.6 |
| Tauri | 614 | 1 | 4 | 5.5 |

## IPC — concurrente (2000 en lotes de 50)

| Framework | ops/s |
|---|---:|
| Owear (con main) | 763 |
| Owear (sin main) | 794 |
| Electron | 6,650 |
| Tauri | 2,128 |

## Payload — eco de texto ida y vuelta (mediana ms)

| Framework | 1 KB | 64 KB | 1 MB | 5 MB |
|---|---:|---:|---:|---:|
| Owear (con main) | 2 | 4.5 | 43 | 210 |
| Owear (sin main) | 2 | 4 | 42 | 203.5 |
| Electron | 0.5 | 2.7 | 19.5 | 77.1 |
| Tauri | 2 | 4.5 | 29 | 136.5 |

## Binario — leer 5 MB nativo→renderer

| Framework | mediana (ms) | MB/s |
|---|---:|---:|
| Owear (con main) | 72 | 69.4 |
| Owear (sin main) | 74 | 67.6 |
| Electron | 63 | 79.5 |
| Tauri | 62 | 80.7 |

## Eventos nativo → renderer (5000)

| Framework | ms | eventos/s |
|---|---:|---:|
| Owear (con main) | 24 | 206,229 |
| Owear (sin main) | 31 | 161,290 |
| Electron | 212 | 23,712 |
| Tauri | 4,894 | 1,022 |

## Cómputo JS (motor)

| Framework | loop 20M (ms) | sort 2M (ms) |
|---|---:|---:|
| Owear (con main) | 30 | 1,231 |
| Owear (sin main) | 28 | 1,200 |
| Electron | 28 | 3,389 |
| Tauri | 28 | 1,250 |

---
Generado por `benchmarks/make_tables.py`.
