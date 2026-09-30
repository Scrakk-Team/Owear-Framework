---
title: Testing
description: Owear has two test layers  fast C++ unit tests and E2E suites that drive a real
order: 5
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Testing

Owear has two test layers: fast C++ unit tests and E2E suites that drive a real
kernel through the control socket.

## Unit tests

Small, dependency-free tests for the core algorithms (minimal JSON, the bridge
codec, SHA-256):

```bash
ctest --test-dir build/linux-release --output-on-failure
```

They run everywhere, including CI, without a display.

## E2E tests

The E2E suites start the real kernel and talk to it through the control socket,
so they need a display. CI uses Xvfb.

Test pages live in `tests/e2e/www/` and the runner is
`tests/e2e/run_suites.py`.

### Page contract

- A page writes results to `window.__R` (a `{ name: string }` map) and sets
  `window.__done = 1` when finished.
- The runner marks **FAIL** if a value starts with `ERR`, `timeout`, or `FAIL`,
  or if the page leaves no results.
- To fail, you must **throw** (or use that prefix). Resolving a promise with the
  string `'ERR …'` does not fail, because `JSON.stringify` wraps it in quotes so
  it no longer starts with `ERR`.

### Running locally

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

## Writing a good test

- Reproduce the **exact** failing path. The project has had live bugs behind a
  green suite because the affected path was untested (for example, the
  `ow-window` bridge was only exercised through the control socket, not the
  renderer).
- **Verify the test fails without the fix** before declaring the change good.
- Prefer a small, focused page per behavior.

## `VERIFY ON REAL DESKTOP`

`ROADMAP.md` marks features that CI can only check structurally — modal dialogs,
tray, global shortcuts. If you touch those areas, update the label with the OS
version where you tested it; the label is only useful if it means something.

## npm packages

```bash
pnpm -r typecheck
pnpm -r build
```

## Related

- [Contributing setup](../contributing/setup.md)
- [Architecture rules](../contributing/architecture-rules.md)
