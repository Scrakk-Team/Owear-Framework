# Benchmarks — Owear vs Electron vs Tauri

**Mediana de 5 repeticiones intercaladas** (round-robin), misma máquina (Linux, Xvfb 1280×800, 4 vCPU).
Reproducir: `python3 benchmarks/run.py --repeat 3`. Detalle crudo: `results.json`.

> Electron embebe **Chromium**; Owear/Tauri usan el **WebView del SO** (WebKitGTK).
> Electron corre `--no-sandbox --disable-gpu` (contenedor sin GPU). Owear "sin main" no arranca Node.

## Arranque, memoria y tamaño

| Framework | Arranque (ms) | RAM idle (MB) | RAM pico (MB) | Procesos | Tamaño (MB) |
|---|---:|---:|---:|---:|---:|
| Owear (con main) | 1,908 | 452.2 | 680.8 | 5 | 6.1 |
| Owear (sin main) | 1,909 | 394.7 | 624.3 | 4 | 6.1 |
| Electron | 1,310 | 633.8 | 794.7 | 8 | 261.9 |
| Tauri | 1,205 | 402.3 | 543 | 4 | 11.6 |

## IPC — round-trip pequeño (secuencial)

| Framework | ops/s | mediana (ms) | p95 (ms) | p99 (ms) |
|---|---:|---:|---:|---:|
| Owear (con main) | 1,417 | 1 | 2 | 2 |
| Owear (sin main) | 1,304 | 1 | 2 | 3 |
| Electron | 2,708 | 0.3 | 0.8 | 1.9 |
| Tauri | 1,025 | 1 | 2 | 3 |

## IPC — concurrente (2000 en lotes de 50)

| Framework | ops/s |
|---|---:|
| Owear (con main) | 4,124 |
| Owear (sin main) | 3,824 |
| Electron | 8,941 |
| Tauri | 2,915 |

## Payload — eco de texto ida y vuelta (mediana ms)

| Framework | 1 KB | 64 KB | 1 MB | 5 MB |
|---|---:|---:|---:|---:|
| Owear (con main) | 1 | 3 | 26 | 138 |
| Owear (sin main) | 1 | 3 | 27 | 137 |
| Electron | 0.4 | 2.9 | 13.4 | 56.5 |
| Tauri | 1 | 3 | 21 | 108 |

## Binario — leer 5 MB nativo→renderer

| Framework | mediana (ms) | MB/s |
|---|---:|---:|
| Owear (con main) | 57 | 87.7 |
| Owear (sin main) | 56 | 89.3 |
| Electron | 41 | 121.1 |
| Tauri | 50 | 100 |

## Descarga — respuesta de 1 MB / 5 MB (mediana ms)

| Framework | 1 MB | 5 MB |
|---|---:|---:|
| Owear (con main) | 14 | 79 |
| Owear (sin main) | 14 | 85 |
| Electron | 8 | 30 |
| Tauri | 12 | 65 |

## Eventos nativo → renderer (5000)

| Framework | ms | eventos/s |
|---|---:|---:|
| Owear (con main) | 20 | 250,000 |
| Owear (sin main) | 23 | 217,391 |
| Electron | 152 | 32,960 |
| Tauri | 4,382 | 1,141 |

## Cómputo JS (motor)

| Framework | loop 20M (ms) | sort 2M (ms) |
|---|---:|---:|
| Owear (con main) | 24 | 995 |
| Owear (sin main) | 24 | 985 |
| Electron | 25 | 2,534 |
| Tauri | 23 | 1,105 |

---
Generado por `benchmarks/make_tables.py`.
