#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# benchmarks/make_tables.py — lee results.json (con repeticiones) y emite RESULTS.md.
import json, os, subprocess, statistics

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(ROOT)
NAME = {"owear": "Owear (con main)", "owear-nom": "Owear (sin main)",
        "electron": "Electron", "tauri": "Tauri"}


def dir_size_mb(p):
    try:
        out = subprocess.run(["du", "-sb", p], capture_output=True, text=True, timeout=30).stdout
        return int(out.split()[0]) / 1024 / 1024
    except Exception:
        return None


SIZES = {
    "owear": lambda: (dir_size_mb(os.path.join(REPO, "build/linux-release/src/owear")) or 0)
    + (dir_size_mb(os.path.join(REPO, "build/linux-release/api")) or 0),
    "owear-nom": lambda: (dir_size_mb(os.path.join(REPO, "build/linux-release/src/owear")) or 0)
    + (dir_size_mb(os.path.join(REPO, "build/linux-release/api")) or 0),
    "electron": lambda: dir_size_mb(os.path.join(ROOT, "electron/node_modules/electron/dist")),
    "tauri": lambda: dir_size_mb(os.path.join(ROOT, "tauri/src-tauri/target/release/bench-tauri")),
}


def avg(vals):
    vals = [v for v in vals if isinstance(v, (int, float))]
    return statistics.median(vals) if vals else None


def get(res, *path):
    cur = res
    for p in path:
        if isinstance(cur, dict):
            cur = cur.get(p)
        else:
            return None
    return cur


def collect(frameworks):
    """Devuelve {framework: {...métricas promediadas...}}"""
    out = {}
    for fw in frameworks:
        runs = [r for r in fw["runs"] if r.get("results")]
        if not runs:
            out[fw["framework"]] = None
            continue
        rs = [r["results"] for r in runs]
        pl, bl, dl = {}, {}, {}
        for sz in (1024, 65536, 1048576, 5242880):
            pl[sz] = avg([next((e["medianMs"] for e in (r.get("payload") or [])
                                if e.get("size") == sz), None) for r in rs])
        for sz in (1048576, 5242880):
            dl[sz] = avg([next((e["medianMs"] for e in (r.get("download") or [])
                                if e.get("size") == sz), None) for r in rs])
        bl[5242880] = avg([get(r, "binary", "medianMs") for r in rs])
        bl["mb"] = avg([get(r, "binary", "mbPerSec") for r in rs])
        out[fw["framework"]] = {
            "startup": avg([r.get("startupMs") for r in runs]),
            "idle": avg([r.get("idleRssMB") for r in runs]),
            "peak": avg([r.get("peakRssMB") for r in runs]),
            "procs": avg([r.get("processes") for r in runs]),
            "ops": avg([get(r, "latency", "opsPerSec") for r in rs]),
            "med": avg([get(r, "latency", "median") for r in rs]),
            "p95": avg([get(r, "latency", "p95") for r in rs]),
            "p99": avg([get(r, "latency", "p99") for r in rs]),
            "conc_ops": avg([get(r, "concurrent", "opsPerSec") for r in rs]),
            "payload": pl,
            "download": dl,
            "bin_med": bl[5242880],
            "bin_mb": bl["mb"],
            "ev_ms": avg([get(r, "events", "ms") for r in rs]),
            "ev_ops": avg([get(r, "events", "eventsPerSec") for r in rs]),
            "js_loop": avg([get(r, "js", "loopMs") for r in rs]),
            "js_sort": avg([get(r, "js", "sortMs") for r in rs]),
        }
    return out


def f(x, d=1):
    if x is None:
        return "—"
    s = f"{x:,.{d}f}"
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    return s


def main():
    with open(os.path.join(ROOT, "results.json")) as fh:
        frameworks = json.load(fh)
    rep = len(frameworks[0]["runs"]) if frameworks else 0
    M = collect(frameworks)
    order = [fw["framework"] for fw in frameworks]

    L = ["# Benchmarks — Owear vs Electron vs Tauri", ""]
    L.append(f"**Mediana de {rep} repeticiones intercaladas** (round-robin), misma máquina (Linux, Xvfb 1280×800, 4 vCPU).")
    L.append("Reproducir: `python3 benchmarks/run.py --repeat 3`. Detalle crudo: `results.json`.")
    L.append("")
    L.append("> Electron embebe **Chromium**; Owear/Tauri usan el **WebView del SO** (WebKitGTK).")
    L.append("> Electron corre `--no-sandbox --disable-gpu` (contenedor sin GPU). Owear \"sin main\" no arranca Node.")
    L.append("")

    def table(title, headers, rowfn):
        L.append(f"## {title}")
        L.append("")
        L.append("| Framework | " + " | ".join(headers) + " |")
        L.append("|---" + "|---:" * len(headers) + "|")
        for fw in order:
            m = M.get(fw)
            if not m:
                L.append(f"| {NAME.get(fw,fw)} | " + " | ".join("—" for _ in headers) + " |")
                continue
            L.append(f"| {NAME.get(fw,fw)} | " + " | ".join(rowfn(m, fw)) + " |")
        L.append("")

    table("Arranque, memoria y tamaño",
          ["Arranque (ms)", "RAM idle (MB)", "RAM pico (MB)", "Procesos", "Tamaño (MB)"],
          lambda m, fw: [f(m["startup"], 0), f(m["idle"]), f(m["peak"]), f(m["procs"], 1),
                         f(SIZES.get(fw, lambda: None)())])

    table("IPC — round-trip pequeño (secuencial)",
          ["ops/s", "mediana (ms)", "p95 (ms)", "p99 (ms)"],
          lambda m, fw: [f(m["ops"], 0), f(m["med"], 2), f(m["p95"], 1), f(m["p99"], 1)])

    table("IPC — concurrente (2000 en lotes de 50)", ["ops/s"],
          lambda m, fw: [f(m["conc_ops"], 0)])

    L.append("## Payload — eco de texto ida y vuelta (mediana ms)")
    L.append("")
    L.append("| Framework | 1 KB | 64 KB | 1 MB | 5 MB |")
    L.append("|---|---:|---:|---:|---:|")
    for fw in order:
        m = M.get(fw)
        cells = ["—"] * 4 if not m else [f(m["payload"][s], 1) for s in (1024, 65536, 1048576, 5242880)]
        L.append(f"| {NAME.get(fw,fw)} | " + " | ".join(cells) + " |")
    L.append("")

    table("Binario — leer 5 MB nativo→renderer", ["mediana (ms)", "MB/s"],
          lambda m, fw: [f(m["bin_med"], 0), f(m["bin_mb"])])

    L.append("## Descarga — respuesta de 1 MB / 5 MB (mediana ms)")
    L.append("")
    L.append("| Framework | 1 MB | 5 MB |")
    L.append("|---|---:|---:|")
    for fw in order:
        m = M.get(fw)
        cells = ["—", "—"] if not m else [f(m["download"][s], 0) for s in (1048576, 5242880)]
        L.append(f"| {NAME.get(fw,fw)} | " + " | ".join(cells) + " |")
    L.append("")

    table("Eventos nativo → renderer (5000)", ["ms", "eventos/s"],
          lambda m, fw: [f(m["ev_ms"], 0), f(m["ev_ops"], 0)])

    table("Cómputo JS (motor)", ["loop 20M (ms)", "sort 2M (ms)"],
          lambda m, fw: [f(m["js_loop"], 0), f(m["js_sort"], 0)])

    L.append("---")
    L.append("Generado por `benchmarks/make_tables.py`.")
    md = "\n".join(L) + "\n"
    with open(os.path.join(ROOT, "RESULTS.md"), "w") as fh:
        fh.write(md)
    print(md)


if __name__ == "__main__":
    main()
