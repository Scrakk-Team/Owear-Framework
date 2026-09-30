---
title: Contributing setup
description: Same as the linux CI job 
order: 4
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Contributing setup

## Requirements (Linux)

Same as the `linux` CI job:

```bash
sudo apt-get install -y libgtk-3-dev libwebkit2gtk-4.1-dev libsoup-3.0-dev \
  libssl-dev zlib1g-dev ninja-build xvfb libayatana-appindicator3-dev
```

Windows needs MSVC + vcpkg (`zlib`, `openssl`) and the WebView2 SDK under
`deps/webview2/`.

## Build and test

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --test-dir build/linux-release --output-on-failure
```

E2E suites start a real kernel and talk to it via the control socket, so they
need a display (CI uses Xvfb):

```bash
MODS=""
for d in build/linux-release/api/*/; do
  [ "$d" != "build/linux-release/api/CMakeFiles/" ] && MODS="$MODS$d:"
done
xvfb-run -a --server-args="-screen 0 1280x800x24" \
  env OW_APP_NAME=CI GDK_BACKEND=x11 OW_MODULES_DIR="${MODS%:}" \
  setsid ./build/linux-release/src/owear > /tmp/owear-e2e.log 2>&1 &
sleep 5
python3 tests/e2e/run_suites.py --pages all.html builtins.html veto.html --port 8123
```

npm packages:

```bash
pnpm -r typecheck
pnpm -r build
```

## Generated code

Manifest-driven files must stay in sync:

```bash
node tools/gen-apis.mjs           # regenerate
node tools/gen-apis.mjs --check   # CI: fail if stale
node tools/check-apis.mjs         # manifest ↔ C++ consistency
```

## Before opening a pull request

1. Build all touched platforms (CI does this; check it is green).
2. Run unit tests and the relevant E2E pages.
3. If you touched a `VERIFY ON REAL DESKTOP` area, say where you tested it.
4. Regenerate manifests if you added/changed an API.

## Related

- [Architecture rules](architecture-rules.md) — the invariants that matter most.
- [Testing](../architecture/testing.md) — writing E2E tests.
- [Adding an API](adding-an-api.md).
