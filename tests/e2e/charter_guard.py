#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tests/e2e/charter_guard.py — the charter refuses what it does not grant.
#
# The charter is the capability policy of a window's own document. This test
# runs against a live kernel and checks, from the renderer's point of view:
#
#   1. a window without a charter can call anything (default behaviour);
#   2. once a charter is installed, everything it does not grant is refused;
#   3. `deny` beats `allow`;
#   4. the refusal is a normal rejection carrying the charter message;
#   5. the kernel broadcasts `charter.denied` (the audit trail);
#   6. `clearCharter` restores the default.
#
import argparse, http.server, json, os, sys, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import owconn

HERE = os.path.dirname(os.path.abspath(__file__))
WWW = os.path.join(HERE, "www")


def serve(port):
    """Serves tests/e2e/www (reuses a server already listening on the port)."""
    os.chdir(WWW)
    try:
        srv = http.server.ThreadingHTTPServer(
            ("127.0.0.1", port), http.server.SimpleHTTPRequestHandler
        )
        threading.Thread(target=srv.serve_forever, daemon=True).start()
    except OSError:
        import urllib.request
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/charter.html", timeout=3) as r:
            assert r.status == 200


class Client:
    """Minimal NDJSON client with its own ids and an event queue."""

    def __init__(self, conn):
        self.conn = conn
        self._id = 7000
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
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            if not isinstance(msg, dict):
                continue
            if msg.get("event"):
                self.events.append(msg)
                continue
            if msg.get("id") == want:
                if msg.get("ok"):
                    return msg.get("result")
                raise RuntimeError(msg.get("error") or f"control error in {cmd}")
        raise RuntimeError(f"timeout waiting for {cmd}")

    def find_event(self, name):
        for ev in self.events:
            if ev.get("event") == name:
                return ev.get("params") or {}
        return None


def eval_json(c, wid, js, timeout=10):
    """window.eval + JSON.parse.

    The kernel wraps the script as `JSON.stringify(( <js> ))`, so the wire value
    is always JSON text and `js` must be a single expression. Pass the value you
    want (`window.foo`), not a pre-stringified one.
    """
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        raw = c.call("window.eval", {"windowId": wid, "js": js})
        try:
            return json.loads(raw) if isinstance(raw, str) else raw
        except (json.JSONDecodeError, TypeError):
            last = raw
            time.sleep(0.2)
    raise RuntimeError(f"eval never returned JSON (last: {last!r})")


def probe(c, wid, timeout=20):
    """Runs the page probe and waits for *that* run's results."""
    before = eval_json(c, wid, "(window.__owCharter && window.__owCharter.run) || 0")
    # Single expression: the eval wrapper is `JSON.stringify(( <js> ))`.
    c.call("window.eval", {"windowId": wid, "js": "window.__owCharterProbe()"})
    deadline = time.time() + timeout
    while time.time() < deadline:
        data = eval_json(c, wid, "window.__owCharter")
        if isinstance(data, dict) and data.get("done") == 1 and \
                data.get("run", 0) > (before or 0):
            return data.get("results") or {}
        time.sleep(0.3)
    raise RuntimeError("probe never finished")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8123)
    ap.add_argument("--pid", type=int, default=0)
    args = ap.parse_args()

    serve(args.port)
    c = Client(owconn.find_kernel_conn(pid=args.pid))
    failures = []
    checks = 0

    def check(name, cond, detail=""):
        nonlocal checks
        checks += 1
        if cond:
            print(f"OK   {name}")
        else:
            print(f"FAIL {name} -> {detail}")
            failures.append(name)

    wid = None
    try:
        url = f"http://localhost:{args.port}/charter.html"
        wid = c.call("window.create", {"width": 640, "height": 420, "url": url})["windowId"]
        time.sleep(2.0)

        # 0. no charter yet: fs is reachable (the default behaviour).
        base = probe(c, wid)
        check("default.grants.fs", str(base.get("fs", "")).startswith("granted:"),
              base.get("fs"))

        # 1. install a minimal charter → fs must be refused, granted calls pass.
        state = c.call("window.setCharter", {
            "windowId": wid,
            "allow": ["ow-window", "theme"],
            "enforce": True,
        })
        check("charter.enforce", bool(state.get("enforce")), state)
        check("charter.allow", sorted(state.get("allow") or []) == ["ow-window", "theme"],
              state)

        res = probe(c, wid)
        check("charter.refuses.fs", str(res.get("fs", "")).startswith("denied:"),
              res.get("fs"))
        check("charter.error.is.clear",
              "charter: capability 'fs:readText' is not granted" in str(res.get("fs", "")),
              res.get("fs"))
        check("charter.grants.theme", str(res.get("theme", "")).startswith("granted:"),
              res.get("theme"))
        # Granted calls may still fail inside the module; what matters here is
        # that the kernel did not refuse them.
        check("charter.grants.ow-window", "charter:" not in str(res.get("owWindow", "")),
              res.get("owWindow"))

        # 2. audit: the kernel tells the main process what it refused.
        denied = c.find_event("charter.denied")
        check("charter.audit.event", denied is not None, "no charter.denied event")
        if denied:
            check("charter.audit.capability", denied.get("capability") == "fs:readText",
                  denied)
            check("charter.audit.window", denied.get("windowId") == wid, denied)

        # 3. deny beats allow (a whole module granted, one function refused).
        c.call("window.setCharter", {
            "windowId": wid,
            "allow": ["fs:*", "ow-window", "theme"],
            "deny": ["fs:readText"],
        })
        res = probe(c, wid)
        check("charter.deny.wins", str(res.get("fs", "")).startswith("denied:"),
              res.get("fs"))

        # 4. clearCharter → back to the default (no filtering).
        c.call("window.clearCharter", {"windowId": wid})
        res = probe(c, wid)
        check("charter.cleared.grants.fs", str(res.get("fs", "")).startswith("granted:"),
              res.get("fs"))
    finally:
        if wid is not None:
            try:
                c.call("window.destroy", {"windowId": wid})
            except Exception:
                pass

    print(f"\nTOTAL: {checks - len(failures)} OK / {len(failures)} FAIL")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
