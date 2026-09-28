#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# benchmarks/run.py — corre el bench de Owear (con y sin main), Electron y Tauri,
# mide arranque + RAM/procesos in-situ y junta los resultados en results.json.
#
#   python3 benchmarks/run.py [--only owear|owear-nom|electron|tauri] [--repeat N]
#
import json, os, signal, subprocess, sys, time

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(ROOT)
OUTDIR = "/tmp/opencode/bench/out"
DISPLAY_OK = False
READY = "BENCH_READY"
CASES = ["owear", "owear-nom", "electron", "tauri"]


def out_path(name):
    return os.path.join(OUTDIR, name + ".json")


def comm(pid):
    try:
        with open(f"/proc/{pid}/stat") as f:
            return f.read().split()[1].strip("()")
    except Exception:
        return ""


def tree(pid):
    kids = {pid}
    changed = True
    while changed:
        changed = False
        for p in os.listdir("/proc"):
            if not p.isdigit():
                continue
            try:
                with open(f"/proc/{p}/stat") as f:
                    ppid = int(f.read().split()[3])
            except Exception:
                continue
            if ppid in kids and int(p) not in kids:
                kids.add(int(p))
                changed = True
    return {p for p in kids if comm(p) != "Xvfb"}


def rss_kb(pids):
    total = 0
    for p in pids:
        try:
            with open(f"/proc/{p}/status") as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        total += int(line.split()[1])
                        break
        except Exception:
            pass
    return total


def stock_modules():
    base = os.path.join(REPO, "build/linux-release/api")
    return ":".join(os.path.join(base, d) for d in os.listdir(base) if d != "CMakeFiles")


def app_cmd(name):
    if name in ("owear", "owear-nom"):
        mods = stock_modules() + ":/tmp/opencode/bench/owear-modules"
        env = dict(os.environ,
                   OW_ASSETS_DIR=os.path.join(ROOT, "owear/assets"),
                   OW_MODULES_DIR=mods,
                   OW_APP_NAME="owearbench", OW_APP_ID="owearbench")
        if name == "owear-nom":
            url = ("http://127.0.0.1:8200/index.html?out=" +
                   out_path("owear-nom").replace("/", "%2F"))
            env.update(OW_DEMO="1", OW_DEV_SERVER_URL=url)
        else:
            env.update(OW_BENCH_URL="http://127.0.0.1:8200/index.html",
                       OW_APP_MAIN=os.path.join(ROOT, "owear/main.mjs"))
        return [os.path.join(REPO, "build/linux-release/src/owear")], env, REPO
    if name == "electron":
        return (["./node_modules/.bin/electron", "--no-sandbox",
                 "--disable-dev-shm-usage", "--disable-gpu", "."],
                dict(os.environ), os.path.join(ROOT, "electron"))
    if name == "tauri":
        return (["./target/release/bench-tauri"], dict(os.environ),
                os.path.join(ROOT, "tauri/src-tauri"))
    raise ValueError(name)


def run_once(name):
    out = out_path(name)
    for f in (out, out + ".done"):
        if os.path.exists(f):
            os.remove(f)
    cmd, env, cwd = app_cmd(name)
    full = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24"] + cmd
    logpath = f"/tmp/opencode/bench/{name}.log"
    logf = open(logpath, "w")
    t0 = time.time()
    proc = subprocess.Popen(full, cwd=cwd, env=env, stdout=logf,
                            stderr=subprocess.STDOUT, start_new_session=True)
    startup = None
    idle = None
    peak = 0
    deadline = t0 + 200
    while time.time() < deadline:
        if startup is None:
            try:
                with open(logpath, "r", errors="ignore") as fh:
                    if READY in fh.read():
                        startup = (time.time() - t0) * 1000
            except Exception:
                pass
        if startup is not None:
            pids = tree(proc.pid)
            r = rss_kb(pids)
            peak = max(peak, r)
            if idle is None and time.time() - t0 > startup / 1000 + 1.5:
                idle = (r, len(pids))
        if os.path.exists(out + ".done"):
            time.sleep(0.4)
            break
        if proc.poll() is not None:
            break
        time.sleep(0.1)
    logf.close()
    for _ in range(20):
        if proc.poll() is not None:
            break
        time.sleep(0.2)
    if proc.poll() is None:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            time.sleep(0.8)
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except Exception:
            pass
    result = None
    if os.path.exists(out):
        try:
            with open(out) as fh:
                result = json.load(fh)
        except Exception:
            result = None
    return {
        "framework": name,
        "startupMs": round(startup, 1) if startup else None,
        "idleRssMB": round(idle[0] / 1024, 1) if idle else None,
        "peakRssMB": round(peak / 1024, 1) if peak else None,
        "processes": idle[1] if idle else None,
        "results": result,
    }


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    repeat = int(sys.argv[sys.argv.index("--repeat") + 1]) if "--repeat" in sys.argv else 1
    names = [only] if only else CASES
    out = []
    for n in names:
        runs = []
        for i in range(repeat):
            r = run_once(n)
            print(f"== {n} run {i+1}/{repeat}: startup={r['startupMs']} idle={r['idleRssMB']} "
                  f"ok={bool(r['results'])} ==", flush=True)
            runs.append(r)
        out.append({"framework": n, "runs": runs})
    path = os.path.join(ROOT, "results.json")
    with open(path, "w") as f:
        json.dump(out, f, indent=2)
    print("escrito", path)


if __name__ == "__main__":
    main()
