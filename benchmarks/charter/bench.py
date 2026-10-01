#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# benchmarks/charter/bench.py — what the capability policy costs.
#
# The charter is enforced by the kernel on every call made from a window's own
# document. This benchmark measures the same sequential round-trip in three
# states, so the policy cost is isolated from the IPC cost:
#
#   · off     — no charter at all (the default behaviour)
#   · allow   — charter installed and the call is granted
#   · deny    — charter installed and the call is refused (still a full trip)
#
# The Electron/Tauri reference numbers come from benchmarks/results.json (same
# machine and methodology). They have no kernel-side capability policy, so only
# Owear can produce the `allow`/`deny` rows: the point of the table is to show
# what enforcing a policy costs on top of an IPC that already sits between them.
#
# Usage: python3 benchmarks/charter/bench.py [--repeat 3] [--n 2000] [--port 8199]
#
import argparse, http.server, json, os, statistics, sys, threading, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tests", "e2e"))
import owconn  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def serve(port):
    handler = lambda *a: http.server.SimpleHTTPRequestHandler(*a, directory=HERE)  # noqa: E731
    for attempt in range(2):
        try:
            srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
            threading.Thread(target=srv.serve_forever, daemon=True).start()
            return
        except OSError:
            if attempt == 0:  # something is already listening: reuse it
                continue
            raise


class Client:
    def __init__(self, conn):
        self.conn = conn
        self._id = 9000

    def call(self, cmd, params=None, timeout=60):
        self._id += 1
        want = self._id
        self.conn.write_line(json.dumps(
            {"id": want, "cmd": cmd, "params": params or {}}).encode())
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.conn.read_line(timeout_s=0.3)
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(msg, dict) and msg.get("id") == want:
                if msg.get("ok"):
                    return msg.get("result")
                raise RuntimeError(msg.get("error") or f"control error in {cmd}")
        raise RuntimeError(f"timeout waiting for {cmd}")

    def eval_json(self, wid, js, timeout=120):
        deadline = time.time() + timeout
        while time.time() < deadline:
            raw = self.call("window.eval", {"windowId": wid, "js": js})
            try:
                return json.loads(raw) if isinstance(raw, str) else raw
            except (json.JSONDecodeError, TypeError):
                time.sleep(0.2)
        raise RuntimeError("eval never returned JSON")


def run_once(c, wid, n):
    """One timed run on the current charter state. Returns (ops/s, µs/call, denied)."""
    c.call("window.eval", {"windowId": wid, "js": f"window.__charterRun({n})"})
    deadline = time.time() + 120
    while time.time() < deadline:
        state = c.eval_json(wid, "window.__charterBench")
        if isinstance(state, dict) and state.get("done") == 1:
            ms = float(state.get("ms") or 0)
            ops = n / (ms / 1000.0) if ms > 0 else 0.0
            return ops, (ms * 1000.0 / n if n else 0.0), int(state.get("denied") or 0)
        time.sleep(0.2)
    raise RuntimeError("bench run never finished")


def set_state(c, wid, charter):
    if charter is None:
        c.call("window.clearCharter", {"windowId": wid})
    else:
        c.call("window.setCharter", {"windowId": wid, **charter})


def reference_ipc():
    """Electron/Tauri round-trip from the main suite (same machine).

    `results.json` stores a list of `{framework, runs:[{results:{latency:{...}}}]}`.
    """
    path = os.path.join(ROOT, "benchmarks", "results.json")
    try:
        with open(path) as fh:
            data = json.load(fh)
    except (OSError, json.JSONDecodeError):
        return {}
    out = {}
    for entry in data.get("frameworks") or []:
        if not isinstance(entry, dict):
            continue
        name = entry.get("framework")
        samples = [
            (run.get("results") or {}).get("latency", {}).get("opsPerSec")
            for run in (entry.get("runs") or [])
        ]
        samples = [s for s in samples if s]
        if name and samples:
            out[name] = statistics.median(samples)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=2000, help="round-trips per run")
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--port", type=int, default=8199)
    ap.add_argument("--pid", type=int, default=0)
    args = ap.parse_args()

    serve(args.port)
    c = Client(owconn.find_kernel_conn(pid=args.pid))

    wid = None
    try:
        wid = c.call("window.create", {
            "width": 640, "height": 420,
            "url": f"http://localhost:{args.port}/bench.html",
        })["windowId"]
        time.sleep(2.0)

        states = [
            ("off (no charter)", None),
            ("charter: allow ['ow-window']",
             {"allow": ["ow-window", "theme"], "enforce": True}),
            ("charter: deny ow-window:isMaximized",
             {"allow": ["theme"], "deny": ["ow-window:isMaximized"], "enforce": True}),
        ]
        samples = {name: {"ops": [], "us": [], "denied": 0} for name, _ in states}
        for name, charter in states:  # one warm-up per state, discarded
            set_state(c, wid, charter)
            run_once(c, wid, max(50, args.n // 10))
        for rnd in range(args.repeat):
            # Latin-square rotation: the state measured first in a round is
            # always the slowest (fresh buffers + GC), so rotate the order.
            shift = rnd % len(states)
            for name, charter in states[shift:] + states[:shift]:
                set_state(c, wid, charter)
                run_once(c, wid, max(50, args.n // 10))  # per-round warm-up
                o, u, d = run_once(c, wid, args.n)
                samples[name]["ops"].append(o)
                samples[name]["us"].append(u)
                samples[name]["denied"] = d
        off, allow, deny = (
            {
                "name": state,
                "ops": statistics.median(samples[state]["ops"]),
                "us": statistics.median(samples[state]["us"]),
                "denied": samples[state]["denied"],
            }
            for state, _ in states
        )
    finally:
        if wid is not None:
            try:
                c.call("window.destroy", {"windowId": wid})
            except Exception:
                pass

    base = off["ops"] or 1.0
    rows = [
        (off["name"], off["ops"], off["us"], "—"),
        (allow["name"], allow["ops"], allow["us"],
         f"+{(allow['us'] / off['us'] - 1) * 100:.1f}%" if off["us"] else "—"),
        (deny["name"], deny["ops"], deny["us"],
         f"+{(deny['us'] / off['us'] - 1) * 100:.1f}%" if off["us"] else "—"),
    ]

    print(f"\nOwear — capability policy cost ({args.n} sequential ow.invoke, "
          f"median of {args.repeat} + warm-up)\n")
    print(f"{'state':32s} {'ops/s':>9s} {'µs/call':>9s} {'overhead':>9s}")
    for name, ops, us, ovh in rows:
        print(f"{name:32s} {ops:9.0f} {us:9.1f} {ovh:>9s}")

    ref = reference_ipc()
    if ref:
        print("\nreference round-trip from benchmarks/results.json "
              "(no policy enforced):")
        for key, ops in sorted(ref.items(), key=lambda kv: -kv[1]):
            print(f"  {key:10s} {ops:8.0f} ops/s   ({1e6 / ops:.0f} µs/call)")

    out = os.path.join(HERE, "RESULTS.md")
    if os.path.exists(out):
        print(f"\n(kept existing {os.path.relpath(out, ROOT)} — delete it to rewrite)")
    else:
        with open(out, "w") as fh:
            fh.write("# Benchmarks — what the charter costs\n\n")
            fh.write(f"Machine and methodology: see `benchmarks/README.md`. "
                     f"{args.n} sequential `ow.invoke('ow-window','isMaximized')` "
                     f"per run, median of {args.repeat} with Latin-square rotation "
                     f"and a discarded warm-up per state.\n\n")
            fh.write("| state | ops/s | µs/call | overhead vs off |\n")
            fh.write("|---|---:|---:|---:|\n")
            for name, ops, us, ovh in rows:
                fh.write(f"| {name} | {ops:.0f} | {us:.1f} | {ovh} |\n")
            if ref:
                fh.write("\n## Reference round-trip (other frameworks, no policy)\n\n")
                fh.write("| framework | ops/s | µs/call |\n|---|---:|---:|\n")
                for key, ops in sorted(ref.items(), key=lambda kv: -kv[1]):
                    fh.write(f"| {key} | {ops:.0f} | {1e6 / ops:.0f} |\n")
                fh.write("\n_A denied call is still a full kernel round-trip, so the "
                         "`allow`/`deny` rows are the policy cost on top of an IPC "
                         "that already sits between Electron and Tauri._\n")
        print(f"wrote {os.path.relpath(out, ROOT)}")


if __name__ == "__main__":
    main()
