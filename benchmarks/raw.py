#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# benchmarks/raw.py — volcado CRUDO: todas las corridas, sin promediar.
import json, os

ROOT = os.path.dirname(os.path.abspath(__file__))


def f(x):
    return "—" if x is None else (f"{x:,.1f}".rstrip("0").rstrip(".") if isinstance(x, float) else f"{x:,}")


def main():
    with open(os.path.join(ROOT, "results.json")) as fh:
        data = json.load(fh)
    d = data.get("frameworks") if isinstance(data, dict) else data
    cols = ["startup", "ram", "seq", "conc", "1k", "64k", "1m", "5m", "dn1m", "dn5m", "ev/s", "jsloop", "jssort"]
    print(f"{'framework':11} {'rep':>3}  " + "  ".join(f"{c:>7}" for c in cols))
    for fw in d:
        for i, r in enumerate(fw["runs"]):
            res = r.get("results") or {}
            lat = res.get("latency") or {}
            con = res.get("concurrent") or {}
            ev = res.get("events") or {}
            js = res.get("js") or {}
            pl = {e["size"]: e.get("medianMs") for e in res.get("payload", [])}
            dl = {e["size"]: e.get("medianMs") for e in res.get("download", [])}
            row = [r.get("startupMs"), r.get("idleRssMB"), lat.get("opsPerSec"),
                   con.get("opsPerSec"), pl.get(1024), pl.get(65536), pl.get(1048576),
                   pl.get(5242880), dl.get(1048576), dl.get(5242880),
                   ev.get("eventsPerSec"), js.get("loopMs"), js.get("sortMs")]
            print(f"{fw['framework']:11} {i+1:>3}  " +
                  "  ".join(f"{f(c):>7}" for c in row))
        print()


if __name__ == "__main__":
    main()
