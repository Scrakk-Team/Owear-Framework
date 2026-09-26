#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tests/e2e/run_suites.py — corre las suites E2E (all/builtins) contra un
# kernel Owear corriendo localmente. Multiplataforma: UDS (Linux/macOS) o
# named pipe (Windows) vía tests/e2e/owconn.py.
#
import argparse, http.server, json, os, sys, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import owconn

HERE = os.path.dirname(os.path.abspath(__file__))
WWW = os.path.join(HERE, "www")


def serve(port):
    os.chdir(WWW)
    handler = lambda *a: http.server.SimpleHTTPRequestHandler(*a)
    try:
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
    except OSError:
        # ya hay un servidor en ese puerto: sirve nuestras páginas, lo reutilizamos
        import urllib.request
        with urllib.request.urlopen(
                f"http://127.0.0.1:{port}/all.html", timeout=3) as r:
            assert r.status == 200


def run_suite(conn, page, timeout_s=60):
    ids = [1000]

    def send(cmd, params):
        ids[0] += 1
        conn.write_line(json.dumps({"id": ids[0], "cmd": cmd,
                                    "params": params}).encode())

    def wait_for(i):
        deadline = time.time() + 15
        while time.time() < deadline:
            line = conn.read_line(timeout_s=15)
            if not line:
                return {"eof": True}
            try:
                m = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(m, dict) and m.get("id") == i:
                return m
        return {"timeout": True}

    send("window.create", {"width": 760, "height": 420, "url": page})
    m = wait_for(ids[0])
    wid = (m.get("result") or {}).get("windowId")
    if not wid:
        print(f"FALLO creando ventana para {page}: {m}")
        return 0, 1, [f"FAIL window.create {page}"]
    time.sleep(2.5)

    result = None
    for _ in range(int(timeout_s / 0.5)):
        send("window.eval",
             {"windowId": wid,
              "js": "JSON.stringify({d:window.__done||0,R:window.__R||{}})"})
        m = wait_for(ids[0])
        try:
            raw = m.get("result") if isinstance(m, dict) else None
            data = json.loads(json.loads(raw)) if isinstance(raw, str) else {}
            if data.get("d") == 1:
                result = data["R"]
                break
        except Exception:
            pass
        time.sleep(0.5)

    # Sin resultados no es "todo bien": la página puede haber muerto (p.ej. el
    # veto de veto.html falló y el kernel cerró la ventana) o su script rompió
    # antes de marcar __done. Antes esto daba "0 OK / 0 fallos" y CI en verde.
    if result is None:
        return 0, 1, [f"FAIL {page}: sin resultados tras {timeout_s}s "
                      f"(¿ventana cerrada o script roto?)"]

    ok = fail = 0
    lines = []
    for k, v in sorted((result or {}).items()):
        bad = str(v).startswith(("ERR", "timeout", "FAIL"))
        # findInPage en entornos sin aceleración está degradado: warning aparte
        is_find = k == "win.findInPage" or "findInPage" in k
        good = not bad and not ('matches":0' in str(v) and is_find)
        ok += good
        fail += not good
        mark = "OK" if good else ("WARN" if is_find else "FAIL")
        lines.append(f"{mark:4s} {k:24s} -> {str(v)[:70]}")
    lines.append(f"--- {ok} OK / {fail} FALLOS ({page})")
    return ok, fail, lines


# ── camino del SDK / proceso principal (control socket) ──────────────────────
#
# Este cliente habla el MISMO NDJSON que @owear/core, así que estas pruebas
# cubren exactamente lo que hace el proceso principal (Node): module.list,
# module.invoke y el veto de cierre con `win.on('closeRequested')`.


class SdkClient:
    """Cliente NDJSON con ids propios y cola de eventos."""

    def __init__(self, conn):
        self.conn = conn
        self._id = 5000
        self.events = []

    def call(self, cmd, params=None, timeout=15):
        self._id += 1
        want = self._id
        self.conn.write_line(json.dumps(
            {"id": want, "cmd": cmd, "params": params or {}}).encode())
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.conn.read_line(timeout_s=0.2)
            if not line:
                continue
            try:
                m = json.loads(line)
            except json.JSONDecodeError:
                continue
            if not isinstance(m, dict):
                continue
            if m.get("event"):
                self.events.append(m)          # los eventos no llevan id
                continue
            if m.get("id") == want:
                if m.get("ok"):
                    return m.get("result")
                raise RuntimeError(m.get("error") or "error de control")
        raise RuntimeError(f"timeout esperando {cmd}")

    def wait_event(self, name, timeout=4.0):
        """Payload del primer `window.event` con ese `name`. None si no llega."""
        deadline = time.time() + timeout
        while True:
            for i, e in enumerate(self.events):
                p = e.get("params") or {}
                if e.get("event") == "window.event" and p.get("name") == name:
                    del self.events[i]
                    return p
            if time.time() >= deadline:
                return None
            line = self.conn.read_line(timeout_s=0.2)
            if not line:
                continue
            try:
                m = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(m, dict):
                self.events.append(m)


def run_sdk_checks(conn):
    """Comprueba el camino del proceso principal contra el kernel vivo."""
    c = SdkClient(conn)
    ok = fail = 0
    lines = []
    state = {}

    def check(name, fn):
        nonlocal ok, fail
        try:
            val = fn()
            ok += 1
            lines.append(f"OK   {name:26s} -> {str(val)[:70]}")
        except Exception as e:                                  # noqa: BLE001
            fail += 1
            lines.append(f"FAIL {name:26s} -> {str(e)[:70]}")

    def t_list():
        mods = c.call("module.list")
        names = [m["name"] for m in mods]
        # ow-window es builtin (App.cpp): está aunque no haya OW_MODULES_DIR
        assert "ow-window" in names, f"falta ow-window; módulos: {names}"
        return f"{len(names)} módulos"

    def t_create():
        res = c.call("window.create",
                     {"width": 420, "height": 300, "url": "about:blank"})
        state["wid"] = res["windowId"]
        return f"windowId={res['windowId']}"

    def t_invoke():
        # ow-window espera [windowId, ...] en el array de args
        return c.call("module.invoke",
                      {"module": "ow-window", "method": "isMaximized",
                       "args": [state["wid"]]})

    def t_invoke_err():
        try:
            c.call("module.invoke", {"module": "no-existe", "method": "x"})
        except RuntimeError as e:
            assert "función desconocida" in str(e), str(e)
            return "error propagado correctamente"
        raise AssertionError("un módulo inexistente no devolvió error")

    def t_veto():
        c.call("window.close", {"windowId": state["wid"]})
        ev = c.wait_event("closeRequested")
        assert ev, "el SDK no recibió closeRequested"
        rid = (ev.get("payload") or {}).get("requestId")
        assert isinstance(rid, int), f"closeRequested sin requestId: {ev}"
        c.call("window.respondCloseRequest",
               {"windowId": state["wid"], "requestId": rid, "allow": False})
        b = c.call("window.getBounds", {"windowId": state["wid"]})
        return f"vetado (requestId={rid}), sigue viva {b['width']}x{b['height']}"

    def t_allow():
        c.call("window.close", {"windowId": state["wid"]})
        ev = c.wait_event("closeRequested")
        assert ev, "no llegó el segundo closeRequested"
        rid = (ev.get("payload") or {}).get("requestId")
        c.call("window.respondCloseRequest",
               {"windowId": state["wid"], "requestId": rid, "allow": True})
        assert c.wait_event("closed"), "allow=true no destruyó la ventana"
        return "cerrada tras allow=true"

    check("sdk.module.list", t_list)
    check("sdk.window.create", t_create)
    check("sdk.module.invoke", t_invoke)
    check("sdk.module.invoke.error", t_invoke_err)
    check("sdk.closeRequested.veto", t_veto)
    check("sdk.closeRequested.allow", t_allow)
    return ok, fail, lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pages", nargs="+",
                    default=["all.html", "builtins.html", "veto.html"])
    ap.add_argument("--port", type=int, default=8123)
    ap.add_argument("--pid", type=int, default=0,
                    help="PID del kernel (Windows: pipe directa)")
    ap.add_argument("--sdk", action="store_true",
                    help="incluye el camino del SDK/proceso principal "
                         "(module.invoke, closeRequested). Verificado en Linux: "
                         "actívalo en otras plataformas tras un pase en verde")
    args = ap.parse_args()

    conn = owconn.find_kernel_conn(pid=args.pid)

    serve(args.port)
    total_ok = total_fail = 0
    for page in args.pages:
        ok, fail, lines = run_suite(conn, f"http://localhost:{args.port}/{page}")
        total_ok += ok
        total_fail += fail
        print("\n".join(lines))

    if args.sdk:
        ok, fail, lines = run_sdk_checks(conn)
        total_ok += ok
        total_fail += fail
        print("\n".join(lines))

    print(f"\nTOTAL: {total_ok} OK / {total_fail} FALLOS")
    sys.exit(1 if total_fail else 0)


if __name__ == "__main__":
    main()
