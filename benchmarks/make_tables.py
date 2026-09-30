#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# benchmarks/make_tables.py — lee results.json (con repeticiones) y emite RESULTS.md.
import json, os, subprocess, statistics

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(ROOT)
NAME = {"owear": "Owear (main)", "owear-nom": "Owear (no main)",
        "electron": "Electron", "tauri": "Tauri", "neutralino": "Neutralino"}


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
    "neutralino": lambda: (dir_size_mb(os.path.join(ROOT, "neutralino/bin/neutralino-linux_x64")) or 0)
    + (dir_size_mb(os.path.join(ROOT, "neutralino/resources")) or 0),
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
            "size": (SIZES.get(fw["framework"], lambda: None)() or None),
        }
    return out


def f(x, d=1):
    if x is None:
        return "—"
    s = f"{x:,.{d}f}"
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    return s


# ── Porcentajes + score + gráficos ──────────────────────────────────────────
# (clave, etiqueta, dirección, unidad) — dirección: -1 mejor=menos, +1 mejor=más
METRICS = [
    ("startup", "Startup", -1, "ms"),
    ("idle", "Idle RAM (PSS)", -1, "MB"),
    ("peak", "Peak RAM (PSS)", -1, "MB"),
    ("size", "Size", -1, "MB"),
    ("ops", "Sequential IPC", +1, "ops/s"),
    ("conc_ops", "Concurrent IPC", +1, "ops/s"),
    ("pl1m", "Payload 1 MB", -1, "ms"),
    ("bin_mb", "File 5 MB", +1, "MB/s"),
    ("dl5m", "Download 5 MB", -1, "ms"),
    ("ev_ops", "Native→render events", +1, "ev/s"),
    ("js_loop", "JS loop 20M", -1, "ms"),
]


def metric_value(m, key):
    if not m:
        return None
    if key == "pl1m":
        return m["payload"].get(1048576)
    if key == "dl5m":
        return m["download"].get(5242880)
    return m.get(key)


def cost(value, direction):
    """Normaliza a "menos es mejor" para comparar peras con manzanas."""
    if value is None or value == 0:
        return None
    return value if direction < 0 else 1.0 / value


def percent_section(M, order):
    base = "electron" if "electron" in order else order[0]
    L = ["## Relative performance (% — baseline = Electron = 100; < 100 = better)", ""]
    L.append("Each cell is `cost(frame) / cost(Electron) × 100`, with the cost normalized")
    L.append("(lower is better; for ops/s it's inverted). **Lower than 100% = better than Electron.**")
    L.append("")
    L.append("| Metric | Owear | Owear (no main) | Tauri | Neutralino | Electron |")
    L.append("|---|---:|---:|---:|---:|---:|")
    ratios = {}
    for key, label, direction, _unit in METRICS:
        cells = []
        base_cost = cost(metric_value(M.get(base), key), direction)
        row_ratio = None
        for fw in ["owear", "owear-nom", "tauri", "neutralino", base]:
            c = cost(metric_value(M.get(fw), key), direction)
            if c is None or base_cost is None:
                cells.append("—")
            else:
                cells.append(f"{c / base_cost * 100:,.0f}%")
            if fw == "owear":
                row_ratio = (c / base_cost) if (c and base_cost) else None
        ratios[key] = row_ratio
        L.append(f"| {label} | " + " | ".join(cells) + " |")
    L.append("")
    # score global: media geométrica de los ratios de Owear vs Electron
    vals = [r for r in ratios.values() if r]
    if vals:
        prod = 1.0
        for v in vals:
            prod *= v
        score = prod ** (1.0 / len(vals)) * 100
        L.append(f"**Owear's net score vs Electron: {score:,.0f}%** "
                 f"(< 100% = better overall; geometric mean of {len(vals)} zones).")
        L.append("")
    return L


def _svg(title, unit, rows):
    w = 660
    h = 52 + 30 * len(rows)
    maxv = max([v for _, v in rows if v] or [1])
    box = w - 300
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',
        '<style>text{font:12px "JetBrains Mono",monospace;fill:#abb2bf}'
        '.t{font:600 14px "JetBrains Mono",monospace;fill:#dcdfe4}'
        '.v{fill:#dcdfe4}.b{fill:#d3869b}</style>',
        f'<text class="t" x="10" y="22">{title} · {unit}</text>',
    ]
    y = 42
    for label, v in rows:
        out.append(f'<text x="10" y="{y+12}">{label}</text>')
        bw = int((v or 0) / maxv * box)
        out.append(f'<rect class="b" x="160" y="{y}" width="{max(bw,1)}" height="16" rx="3"/>')
        txt = f"{v:,.1f}" if v else "—"
        out.append(f'<text class="v" x="{168+bw}" y="{y+13}">{txt}</text>')
        y += 30
    out.append("</svg>")
    return "\n".join(out)


def svg_charts(M, order):
    charts = os.path.join(ROOT, "charts")
    os.makedirs(charts, exist_ok=True)
    for key, label, _d, unit in METRICS:
        rows = [(NAME.get(fw, fw), metric_value(M.get(fw), key)) for fw in order if M.get(fw)]
        with open(os.path.join(charts, key + ".svg"), "w") as fh:
            fh.write(_svg(label, unit, rows))


PACKAGED = [
    ("Owear — app (single binary)", os.path.join(REPO, "examples/demo/release/owear-example-demo")),
    ("Owear — installer", os.path.join(REPO, "examples/demo/release/owear-example-demo-installer")),
    ("Owear — uninstaller", os.path.join(REPO, "examples/demo/release/owear-example-demo-uninstaller")),
    ("Electron — framework (dist)", os.path.join(ROOT, "electron/node_modules/electron/dist")),
    ("Tauri — binary", os.path.join(ROOT, "tauri/src-tauri/target/release/bench-tauri")),
    ("Neutralino — dist (bin + resources.neu)", os.path.join(ROOT, "neutralino/dist")),
]


def size_mb(p):
    if not os.path.exists(p):
        return None
    if os.path.isfile(p):
        return os.path.getsize(p) / 1024 / 1024
    return dir_size_mb(p)


def packaged_section():
    L = ["## Packaged artifacts (MB)", ""]
    L.append("| Artifact | MB |")
    L.append("|---|---:|")
    for label, p in PACKAGED:
        mb = size_mb(p)
        L.append(f"| {label} | " + ("—" if mb is None else f"{mb:,.1f}") + " |")
    L.append("")
    L.append("> Owear/Tauri/Neutralino do **not** bundle the system WebView (they use the OS one); "
             "Electron ships Chromium. Owear's installer includes the installer + the app payload.")
    L.append("")
    return L


def main():
    with open(os.path.join(ROOT, "results.json")) as fh:
        data = json.load(fh)
    meta = data.get("meta") if isinstance(data, dict) else None
    frameworks = data.get("frameworks") if isinstance(data, dict) else data
    rep = len(frameworks[0]["runs"]) if frameworks else 0
    M = collect(frameworks)
    order = [fw["framework"] for fw in frameworks]

    L = ["# Benchmarks — Owear vs Electron vs Tauri vs Neutralino", ""]
    L.append(f"**Mediana de {rep} repeticiones intercaladas** (round-robin) + 1 de warmup descartada.")
    L.append("Misma máquina (Linux, Xvfb 1280×800 compartido, GPU software). Arranque por **marcador** (5 ms);")
    L.append("memoria en **PSS** (proporcional). Reproducir: `python3 benchmarks/run.py --warmup 1 --repeat 3`.")
    L.append("Metodología y caveats: `benchmarks/README.md`. Crudo + entorno: `results.json`.")
    L.append("")
    L.append("> Electron embebe **Chromium**; Owear/Tauri/Neutralino usan el **WebView del SO** (WebKitGTK). GPU software en todos.")
    L.append("> Owear \"sin main\" no arranca el sidecar Node. Neutralino usa una **extensión propia** para el IPC.")
    if meta:
        L.append(f"> Entorno: {meta.get('cpu')} · {meta.get('cores')} cores · governor {meta.get('governor')} · "
                 f"kernel {meta.get('kernel')} · WebKitGTK {meta.get('webkitgtk')}")
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

    table("Startup, memory and size",
          ["Startup (ms)", "Idle RAM (MB)", "Peak RAM (MB)", "Processes", "Size (MB)"],
          lambda m, fw: [f(m["startup"], 0), f(m["idle"]), f(m["peak"]), f(m["procs"], 1),
                         f(SIZES.get(fw, lambda: None)())])

    table("IPC — small round-trip (sequential)",
          ["ops/s", "median (ms)", "p95 (ms)", "p99 (ms)"],
          lambda m, fw: [f(m["ops"], 0), f(m["med"], 2), f(m["p95"], 1), f(m["p99"], 1)])

    table("IPC — concurrent (2000 in batches of 50)", ["ops/s"],
          lambda m, fw: [f(m["conc_ops"], 0)])

    L.append("## Payload — text echo round-trip (median ms)")
    L.append("")
    L.append("| Framework | 1 KB | 64 KB | 1 MB | 5 MB |")
    L.append("|---|---:|---:|---:|---:|")
    for fw in order:
        m = M.get(fw)
        cells = ["—"] * 4 if not m else [f(m["payload"][s], 1) for s in (1024, 65536, 1048576, 5242880)]
        L.append(f"| {NAME.get(fw,fw)} | " + " | ".join(cells) + " |")
    L.append("")

    table("File — read 5 MB native→renderer", ["median (ms)", "MB/s"],
          lambda m, fw: [f(m["bin_med"], 0), f(m["bin_mb"])])

    L.append("## Download — 1 MB / 5 MB response (median ms)")
    L.append("")
    L.append("| Framework | 1 MB | 5 MB |")
    L.append("|---|---:|---:|")
    for fw in order:
        m = M.get(fw)
        cells = ["—", "—"] if not m else [f(m["download"][s], 0) for s in (1048576, 5242880)]
        L.append(f"| {NAME.get(fw,fw)} | " + " | ".join(cells) + " |")
    L.append("")

    table("Native → renderer events (5000)", ["ms", "events/s"],
          lambda m, fw: [f(m["ev_ms"], 0), f(m["ev_ops"], 0)])

    table("JS compute (engine)", ["loop 20M (ms)", "sort 2M (ms)"],
          lambda m, fw: [f(m["js_loop"], 0), f(m["js_sort"], 0)])

    L.extend(packaged_section())
    L.extend(percent_section(M, order))
    svg_charts(M, order)

    L.append("## Charts")
    L.append("")
    for key, label, _d, unit in METRICS:
        L.append(f"### {label} ({unit})")
        L.append("")
        L.append(f"![{label}](charts/{key}.svg)")
        L.append("")

    L.append("---")
    L.append("Generado por `benchmarks/make_tables.py`.")
    md = "\n".join(L) + "\n"
    with open(os.path.join(ROOT, "RESULTS.md"), "w") as fh:
        fh.write(md)
    print(md)


if __name__ == "__main__":
    main()
