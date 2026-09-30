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
CASES = ["owear", "owear-nom", "electron", "tauri", "neutralino"]
DISPLAY = ":97"


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


def pss_kb(pids):
    """Memoria proporcional (PSS): reparte las páginas compartidas entre
    procesos. Crítico para Electron, cuyos procesos comparten mucha memoria
    (con RSS se cuenta varias veces)."""
    total = 0
    for p in pids:
        try:
            with open(f"/proc/{p}/smaps_rollup") as f:
                for line in f:
                    if line.startswith("Pss:"):
                        total += int(line.split()[1])
                        break
        except Exception:
            pass
    return total


def metadata():
    """Entorno + versiones, para que el resultado sea reproducible/auditable."""
    def read(path, default=None):
        try:
            with open(path) as f:
                return f.read().strip() or default
        except Exception:
            return default

    def run(cmd):
        try:
            return subprocess.run(cmd, capture_output=True, text=True, timeout=10).stdout.strip()
        except Exception:
            return None

    return {
        "cpu": next((l.split(":", 1)[1].strip() for l in read("/proc/cpuinfo", "").splitlines()
                     if l.startswith("model name")), None),
        "cores": os.cpu_count(),
        "governor": read("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"),
        "kernel": run(["uname", "-r"]),
        "os": read("/etc/os-release", "").split("\n")[0].replace("PRETTY_NAME=", "").strip('"'),
        "node": run(["node", "-v"]),
        "python": run(["python3", "-V"]),
        "owear": run(["git", "-C", REPO, "describe", "--tags", "--always"]),
        "electron": run(["node", "-p",
                         f"require('{os.path.join(ROOT, 'electron/node_modules/electron/package.json')}').version"]),
        "webkitgtk": run(["pkg-config", "--modversion", "webkit2gtk-4.1"]),
        "neutralino": "6.9.0",
        "flags": "gpu=off/software; xvfb 1280x800; electron --no-sandbox --disable-gpu",
    }


def stock_modules():
    base = os.path.join(REPO, "build/linux-release/src/api")
    return ":".join(os.path.join(base, d) for d in os.listdir(base) if d != "CMakeFiles")


def app_cmd(name):
    if name in ("owear", "owear-nom"):
        mods = stock_modules() + ":/tmp/opencode/bench/owear-modules"
        env = dict(os.environ,
                   OW_ASSETS_DIR=os.path.join(ROOT, "owear/assets"),
                   OW_MODULES_DIR=mods,
                   OW_APP_NAME="owearbench", OW_APP_ID="owearbench")
        # Headless/CI: sin renderer GPU (~-300 ms, -30 MB). En desktop real,
        # OW_GPU=auto/on mantiene la aceleración.
        env.setdefault("OW_GPU", "off")
        if name == "owear-nom":
            url = ("http://127.0.0.1:8200/index.html?out=" +
                   out_path("owear-nom").replace("/", "%2F"))
            env.update(OW_START_URL=url)
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
    if name == "neutralino":
        return (["bin/neutralino-linux_x64", "--load-dir-res", "--path=.", "--url=/",
                 "--window-size=900,700"], dict(os.environ), os.path.join(ROOT, "neutralino"))
    raise ValueError(name)


def run_once(name):
    out = out_path(name)
    for f in (out, out + ".done"):
        if os.path.exists(f):
            os.remove(f)
    if name.startswith("owear"):
        # WebKit cachea los assets servidos por HTTP de forma persistente por app
        import shutil
        for p in (os.path.expanduser("~/.local/share/owear/owearbench"),
                  os.path.expanduser("~/.cache/owear/owearbench")):
            shutil.rmtree(p, ignore_errors=True)
    cmd, env, cwd = app_cmd(name)
    env["DISPLAY"] = DISPLAY  # Xvfb compartido (se arranca una vez en main())
    ready_marker = out.replace(".json", ".ready")
    for f in (ready_marker,):
        if os.path.exists(f):
            os.remove(f)
    logpath = f"/tmp/opencode/bench/{name}.log"
    logf = open(logpath, "w")
    t0 = time.time()
    proc = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=logf,
                            stderr=subprocess.STDOUT, start_new_session=True)
    startup = None
    idle = None
    peak = 0
    peak_rss = 0
    deadline = t0 + 300
    # arranque: muestreo fino (5 ms) hasta el marcador de "página lista"
    while startup is None and time.time() < deadline:
        if os.path.exists(ready_marker):
            startup = (time.time() - t0) * 1000
            break
        if proc.poll() is not None:
            break
        time.sleep(0.005)
    while time.time() < deadline:
        pids = tree(proc.pid)
        r = rss_kb(pids)
        ps = pss_kb(pids)
        peak = max(peak, ps)      # PSS (memoria proporcional)
        peak_rss = max(peak_rss, r)
        if idle is None and startup is not None and time.time() - t0 > startup / 1000 + 1.5:
            idle = (ps or r, len(pids))
        if os.path.exists(out + ".done"):
            time.sleep(0.4)
            break
        if proc.poll() is not None:
            break
        time.sleep(0.02)
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
        "idleRssMB": round(idle[0] / 1024, 1) if idle else None,      # PSS idle
        "peakRssMB": round(peak / 1024, 1) if peak else None,         # PSS pico
        "peakRssRawMB": round(peak_rss / 1024, 1) if peak_rss else None,  # RSS crudo
        "processes": idle[1] if idle else None,
        "results": result,
    }


def start_xvfb():
    """Un Xvfb compartido (DISPLAY=:97) → quita el coste variable de xvfb-run."""
    if os.path.exists(f"/tmp/.X11-unix/X{DISPLAY.lstrip(':')}"):
        return
    subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1280x800x24"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.2)


def start_static_server():
    """Sirve benchmarks/owear/assets en :8200 (assets del bench de Owear)."""
    import http.server
    import threading
    import functools
    root = os.path.join(ROOT, "owear/assets")
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=root)
    try:
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", 8200), handler)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
    except OSError:
        pass  # ya hay uno escuchando


def drop_caches():
    """Vacía la caché de disco (cold start). Requiere sudo (BENCH_SUDO_PW)."""
    pw = os.environ.get("BENCH_SUDO_PW")
    if not pw:
        return False
    try:
        subprocess.run(["sudo", "-S", "-p", "", "sh", "-c", "sync; echo 3 > /proc/sys/vm/drop_caches"],
                       input=pw + "\n", text=True, capture_output=True, timeout=30)
        return True
    except Exception:
        return False


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    args = sys.argv
    only = args[args.index("--only") + 1] if "--only" in args else None
    repeat = int(args[args.index("--repeat") + 1]) if "--repeat" in args else 1
    warmup = int(args[args.index("--warmup") + 1]) if "--warmup" in args else 0
    cold = "--cold" in args
    names = [only] if only else CASES
    start_xvfb()
    start_static_server()
    if cold:
        print("cold:", "drop_caches OK" if drop_caches() else "sin BENCH_SUDO_PW", flush=True)
    # warmup: corre cada framework sin registrar (JIT/cachés calientes)
    for _ in range(warmup):
        for n in names:
            run_once(n)
    # Round-robin: se alternan los frameworks en cada repetición para repartir
    # el efecto de la carga de la máquina (menos sesgo de orden).
    runs = {n: [] for n in names}
    for rep in range(repeat):
        for n in names:
            if cold:
                drop_caches()
            r = run_once(n)
            runs[n].append(r)
            print(f"== {n} rep {rep+1}/{repeat}: startup={r['startupMs']} "
                  f"idlePSS={r['idleRssMB']} ok={bool(r['results'])} ==", flush=True)
    out = [{"framework": n, "runs": runs[n]} for n in names]
    path = os.path.join(ROOT, "results.json")
    with open(path, "w") as f:
        json.dump({"meta": metadata(), "frameworks": out}, f, indent=2)
    print("escrito", path)


if __name__ == "__main__":
    main()
