---
title: Adding an API
description: An API is a folder api/<name/ with a manifest, a CMake recipe, and sources.
order: 1
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Adding an API

An API is a folder `api/<name>/` with a manifest, a CMake recipe, and sources.
The manifest is the single source of truth, so you do not edit any list by hand.

## Quick path

```bash
ow api new my-api
# edit api/my-api/src/my-api.cpp and api/my-api/owear.module.json
cmake --build --preset linux-release
```

`ow api new` creates:

```text
api/my-api/
  owear.module.json      # manifest: name, kind, version, description, platforms, functions
  CMakeLists.txt         # ow_add_module(my-api SOURCES src/my-api.cpp)
  README.md
  src/my-api.cpp         # skeleton with a `ping` function
```

and runs `tools/gen-apis.mjs` to regenerate discovery and registration.

Call it from a renderer as `await ow.invoke('my-api', 'ping')`.

## 1. Write the manifest

For a standalone `.owm` module:

```json
{
  "$schema": "../owear.module.schema.json",
  "name": "my-api",
  "kind": "module",
  "version": "0.1.0",
  "description": "What it does.",
  "platforms": ["linux", "win"],
  "optional": false,
  "functions": ["ping", "doThing"]
}
```

`functions` must match the C++ descriptor table **exactly** (including any
lambdas).

For a kernel-coupled builtin use `"kind": "builtin"` with a `descriptors` array
(name, factory, platforms, functions) and per-platform `sources`.

## 2. Implement the functions

```cpp
// api/my-api/src/my-api.cpp
#include <ow/Json.h>
#include <ow/Module.h>

static void ping(const ow_request_t*, ow_response_t* res) {
    ow::Module::RespondOk(res, "\"pong\"");
}

static void doThing(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray())
        return ow::Module::RespondError(res, "invalid args");
    // … do the work, never throw …
    ow::Module::RespondOk(res, "null");
}

OW_MODULE_BEGIN(my-api, "1.0.0")
OW_FN(ping)
OW_FN(doThing)
OW_MODULE_END()
```

Follow the four [architecture rules](architecture-rules.md): no `#ifdef` in shared
code, one source per platform chosen by CMake, respect the ABI memory contract.

## 3. Per-platform sources

Name files with the platform suffix and select them in `CMakeLists.txt`:

```cmake
ow_add_module(my-api
  SOURCES
    src/common.cpp
    src/my-api_${OW_PLATFORM}.${OW_SRC_EXT}
  LIBS
    ...)
```

## 4. Regenerate and validate

```bash
node tools/gen-apis.mjs           # regenerate generated.cmake + builtins
node tools/check-apis.mjs         # manifest ↔ C++ must match exactly
```

`gen-apis.mjs --check` is run in CI and fails if generated files are stale.

## 5. Add tests

Add an E2E page in `tests/e2e/www/` that exercises the new function(s) through the
**renderer bridge** (not only the control socket). Verify the page fails before
your fix and passes after. See [Testing](../architecture/testing.md).

## 6. Document it

Add a page under `docs/api/modules/<name>.md` and link it from
`docs/api/modules/README.md`. Model it on an existing module page.

## Checklist

- [ ] Manifest `functions` matches the descriptor table exactly.
- [ ] `ow api new` (or the generator) has been run, or `gen-apis --check` passes.
- [ ] No `#ifdef` platform branches in common code.
- [ ] No exceptions cross the ABI; buffers obey the memory contract.
- [ ] Large payloads use shared memory.
- [ ] E2E coverage added and verified (red before, green after).
- [ ] Docs page added.
