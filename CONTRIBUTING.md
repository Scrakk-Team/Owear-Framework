<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Contributing to Owear

Thanks for your interest. This document covers what you cannot infer by reading
the code: the architecture rules that hold the project together and how to
verify a change before sending it.

## Requirements (Linux)

The same as the CI `linux` job:

```bash
sudo apt-get install -y libgtk-3-dev libwebkit2gtk-4.1-dev libsoup-3.0-dev \
  libssl-dev zlib1g-dev ninja-build xvfb libayatana-appindicator3-dev
```

Windows needs MSVC + vcpkg (zlib/openssl) and the WebView2 SDK under
`deps/webview2/`.

## Build and test

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --test-dir build/linux-release --output-on-failure
```

The E2E suites start a real kernel and talk to it over the Control Socket, so
they need a display (CI uses Xvfb):

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

For the npm packages: `pnpm -r typecheck` and `pnpm -r build`.

## Architecture rules

These four rules explain most decisions in the repo. Breaking them breaks the
build or, worse, silently breaks a platform.

1. **Anti-drift: public contracts live in `include/ow/`.** They are used by all
   three platforms and by user modules. If you change a signature, the change
   hurts at compile time — which is exactly what we want.

2. **One implementation per platform, chosen by CMake, never by `#ifdef`.**
   `src/**/*_linux.cpp` / `*_win.cpp` are selected in the corresponding
   `CMakeLists.txt`. Do not put platform branches inside a common file.

3. **All UI work goes through the main loop, via `App::Post`.** That is what makes
   it safe for a module with its own thread to touch windows. `App::Post` is
   thread-safe; the rest of the window API is not.

4. **The module ABI (`ow_api.h`) is C, with an explicit memory contract.**
   `ow_response_t` buffers live only during the call: the host copies them
   immediately. Never throw an exception across the host boundary.

## Adding an API

Each `api/<name>/` folder is an independent API with its own target:

```
api/<name>/
  CMakeLists.txt          # ow_add_module(<name> SOURCES … LIBS …)
  src/basic.cpp           # ow_fn_entry_t table + ow_module_descriptor()
```

and one `add_subdirectory(<name>)` line in `api/CMakeLists.txt`. Declare the
functionality in the descriptor table, not in an `if` on the function name.

Before "fixing" something that looks broken, check whether a test covers it and
add the missing one: the project has had live bugs behind a green suite because
the affected path was not covered (for example, the `ow-window` bridge was only
tested through the Control Socket, not from the renderer).

## Writing E2E tests

Test pages live in `tests/e2e/www/`. The contract with the runner
(`tests/e2e/run_suites.py`) is:

- the page leaves its results in `window.__R` (`{ name: string }`) and sets
  `window.__done = 1` when finished;
- the runner marks **FAIL** if a value starts with `ERR`, `timeout`, or `FAIL`,
  and if the page leaves no results. To fail, you must **throw** an exception
  (or use that prefix): resolving a promise with the string `'ERR …'` does not
  work, because `JSON.stringify` of `__R` wraps it in quotes so it no longer
  starts with `ERR`;
- verify your test fails without the fix before declaring the change good.

## Roadmap and `VERIFY ON REAL DESKTOP`

[`docs/roadmap.md`](docs/roadmap.md) marks with that label what CI checks only
partially (modal dialogs, tray, global shortcuts). If you touch those areas,
update the label with the OS version where you tested it — its value lies in it
meaning something.
