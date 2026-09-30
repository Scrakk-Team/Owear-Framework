#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tests/e2e/update_apply.py — prueba REAL del auto-update nativo contra un kernel
# copiado en un directorio temporal (no toca el binario de build):
#
#   state()  → exe, hasRollback=false
#   apply()  → reemplazo atómico + relaunch (mismo PID) y backup <exe>.owprev
#   state()  → hasRollback=true
#   rollback() → restaura el binario anterior y consume el backup
#   commit() → confirma (borra el backup)
#
# Requiere: un Display (usa Xvfb) y el build Linux en build/linux-release.
#
import json, os, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import owconn  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
KERNEL = os.path.join(ROOT, "build", "linux-release", "src", "owear")
APIS = os.path.join(ROOT, "build", "linux-release", "src", "api")


def mods_dir():
    dirs = [os.path.join(APIS, d) for d in os.listdir(APIS)
            if os.path.isdir(os.path.join(APIS, d)) and d != "CMakeFiles"]
    return ":".join(dirs)


def call(conn, cmd, params=None, timeout=20):
    conn.write_line(json.dumps({"id": 1, "cmd": cmd, "params": params or {}}).encode())
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = conn.read_line(timeout_s=1.0)
        if not line:
            continue
        try:
            m = json.loads(line)
        except Exception:
            continue
        if isinstance(m, dict) and m.get("id") == 1:
            return m
    raise RuntimeError(f"timeout {cmd}")


def invoke(conn, method, args=None):
    return call(conn, "module.invoke",
                {"module": "updater", "method": method, "args": args or []})


def wait_binary(path, predicate, timeout=15):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with open(path, "rb") as f:
                if predicate(f.read()):
                    return True
        except FileNotFoundError:
            pass
        time.sleep(0.2)
    return False


def main():
    if not os.path.exists(KERNEL):
        print(f"SKIP: no existe {KERNEL} (compila el kernel Linux)")
        return 0

    work = tempfile.mkdtemp(prefix="owear-apply-")
    exe = os.path.join(work, "MiApp")
    new = os.path.join(work, "MiApp-v2")
    shutil.copy2(KERNEL, exe)
    shutil.copy2(KERNEL, new)
    marker = b"OWUPDATEMARKER-ABC123"
    with open(new, "ab") as f:
        f.write(b"\n" + marker + b"\n")

    os.system("rm -f /tmp/owear-*.sock \"$XDG_RUNTIME_DIR\"/owear-*.sock 2>/dev/null")
    xvfb = None
    env = dict(os.environ, OW_APP_NAME="UpdateApply", OW_MODULES_DIR=mods_dir(),
               OW_APP_VERSION="1.0.0", GDK_BACKEND="x11")
    if not os.environ.get("DISPLAY"):
        xvfb = subprocess.Popen(["Xvfb", ":97", "-screen", "0", "1024x768x24"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.0)
        env["DISPLAY"] = ":97"

    proc = subprocess.Popen([exe], env=env, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, start_new_session=True)
    pid = proc.pid
    results = []

    def step(name, ok, detail=""):
        results.append((name, ok))
        print(f"{'OK  ' if ok else 'FAIL'} {name:22s} {detail}")

    try:
        conn = owconn.find_kernel_conn(pid=pid, wait_s=30)
        st = invoke(conn, "state").get("result") or {}
        step("state.exé inicial", st.get("exe") == exe, st.get("exe"))
        step("state.sin rollback", st.get("hasRollback") is False, str(st.get("hasRollback")))

        # apply (backup por defecto) → el kernel se reemplaza y relanza
        try:
            invoke(conn, "apply", [{"path": new}])
        except Exception:
            pass
        step("apply.reemplaza", wait_binary(exe, lambda b: marker in b), "marcador presente")
        step("apply.backup", os.path.exists(exe + ".owprev"), exe + ".owprev")

        conn = owconn.find_kernel_conn(pid=pid, wait_s=20)
        st = invoke(conn, "state").get("result") or {}
        step("state.hasRollback", st.get("hasRollback") is True, str(st.get("hasRollback")))

        # rollback → vuelve el binario anterior y se consume el backup
        try:
            invoke(conn, "rollback")
        except Exception:
            pass
        step("rollback.restaura", wait_binary(exe, lambda b: marker not in b), "marcador ausente")
        step("rollback.consumido", not os.path.exists(exe + ".owprev"), "backup borrado")

        conn = owconn.find_kernel_conn(pid=pid, wait_s=20)
        st = invoke(conn, "state").get("result") or {}
        step("state.sin rollback", st.get("hasRollback") is False, str(st.get("hasRollback")))

        # commit sin backup pendiente → no-op sin error
        r = invoke(conn, "commit")
        step("commit.no-op", r.get("ok") is True, "")
    finally:
        try:
            proc.kill()
        except Exception:
            pass
        if xvfb:
            xvfb.terminate()
        shutil.rmtree(work, ignore_errors=True)

    failed = [n for n, ok in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} OK")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
