---
title: Native modules (.owm)
description: A native module is a compiled shared library that the kernel loads at runtime
order: 14
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Native modules (.owm)

A native module is a compiled shared library that the kernel loads at runtime
with `dlopen`. You write C++, declare the functions, and call them from
TypeScript like any other module. This is how you push hot paths, hardware
access, or existing C/C++ code into an Owear app.

## Where modules live in your app

Put `.cpp` files in `native/`. The Vite plugin and the CLI compile each file to
`<name>.owm` and load it at runtime.

```text
native/
└── files.cpp        →  dist/modules/files.owm  →  ow.invoke('files', …)
```

In dev, `ow dev` compiles them to `.owear/modules/` and adds that directory to
`OW_MODULES_DIR`.

## A module in 15 lines

```cpp
// native/files.cpp
#include <ow/Json.h>
#include <ow/Module.h>

static void readText(const ow_request_t* req, ow_response_t* res) {
    // req->json holds a JSON array of the arguments.
    // For readText(path): ["/etc/hostname"]
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return ow::Module::RespondError(res, "path required");
    const std::string path = parsed.value->AsArray()[0].AsString();

    // ... read the file ...
    ow::Module::RespondOk(res, ow::json::Value(contents).Serialize().c_str());
}

OW_MODULE_BEGIN(files, "1.0.0")
OW_FN(readText)
OW_MODULE_END()
```

Call it from the renderer:

```ts
import { files } from '@owear/native'
const text = await files.readText('/etc/hostname')
```

## The ABI

The ABI is C and small. A module exports exactly one symbol,
`ow_module_descriptor`, returning a `ow_module_desc_t`:

```cpp
typedef void (*ow_fn_t)(const ow_request_t*, ow_response_t*);

typedef struct {
    const char* name;          // function name
    ow_fn_t fn;                // function pointer
} ow_fn_entry_t;

typedef struct {
    const char* name;          // module name
    const char* version;       // semantic version
    const ow_fn_entry_t* fns;  // function table
    uint32_t fn_count;
} ow_module_desc_t;
```

The macros generate this from a flat table:

```cpp
OW_MODULE_BEGIN(files, "1.0.0")
OW_FN(readText)
OW_FN(writeText)
OW_MODULE_END()
```

### `ow_request_t`

```cpp
typedef struct ow_request_t {
    const char* json;      // JSON array of arguments
    uint32_t    json_len;
    const uint8_t* bin;    // optional binary argument
    uint32_t    bin_len;
    uint32_t    window_id; // window that invoked (0 if none)
    void*       host;      // opaque host handle
} ow_request_t;
```

### `ow_response_t`

```cpp
typedef struct ow_response_t {
    int         status;    // 0 = ok, non-zero = error
    const char* error;     // error message when status != 0
    const char* json;      // JSON result
    uint32_t    json_len;
    const uint8_t* bin;    // optional binary result
    uint32_t    bin_len;
} ow_response_t;
```

**Memory contract:** buffers you return through `res` are only valid during the
call. The host copies them immediately. Recommended helpers
(`ow::Module::RespondOk` / `RespondError`) use a thread-local buffer that lives
until the next call on that thread, which is safe under this contract.

**Never throw across the ABI.** Wrap risky code in `try/catch` and return an
error via `RespondError`.

## Available headers

| Header | Contents |
|---|---|
| `ow_api.h` | `ow_module_desc_t`, `ow_fn_entry_t`, `ow_request_t`, `ow_response_t`, `OW_MODULE_EXPORT` |
| `ow/Module.h` | `OW_MODULE_BEGIN/FN/END`, `Module::RespondOk`, `Module::RespondError` |
| `ow/Json.h` | `json::Parse(sv) → ParseResult`, `Value` (Null/Bool/Int/Double/String/Array/Object), `.Find()`, `.As*`, `.Serialize()`, `json::JsLiteral()` |
| `ow/Base64.h` | `b64::Encode(string_view)`, `b64::Decode(sv, out)` |
| `ow/Shm.h` | `ow_shm_put(data, len) → id`, `ow_shm_data(id, &len) → ptr`, `ow_shm_shutdown()` |

## Returning large binaries

For payloads ≥ 256 KB, publish them to shared memory and return a handle instead
of embedding base64. The renderer reads them with `ow.readShared`.

```cpp
const char* id = ow_shm_put(reinterpret_cast<const uint8_t*>(png.data()), png.size());
std::string json = "{\"__ow_shm\":{\"id\":\"" + std::string(id) +
                   "\",\"size\":" + std::to_string(png.size()) + "}}";
ow::Module::RespondOk(res, json.c_str());
```

## Emitting events

Modules can push events to the renderer. They receive a host handle at load
time:

```cpp
extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    g_host = h;
}

// later, from any thread the host allows:
g_host->emit_event(g_host->ctx, window_id, "files.changed", payloadJson);
```

The renderer receives it with `ow.on('files.changed', cb)`.

## Two kinds of API: `module` vs `builtin`

| Kind | What it is | How it loads |
|---|---|---|
| `module` | Compiles to a standalone `.owm` | `dlopen` at runtime from `OW_MODULES_DIR` / `<exe>/modules` |
| `builtin` | Coupled to the kernel (needs the WebView, `ControlServer`, etc.) | Linked into the binary; registered by `RegisterGeneratedBuiltins()` |

As an app developer you almost always write `module`s. Framework contributors
add `builtin`s inside the Owear repository (see
[Adding an API](../contributing/adding-an-api.md)).

## Building

The Vite plugin runs `owear-build-native` for your app automatically. If you need
to call it directly:

```bash
OW_MODULES_OUT=dist/modules owear-build-native
```

It compiles every `native/*.cpp` and links against the framework headers found
via `OW_INCLUDE_DIR`.

## Checklist

- [ ] `OW_MODULE_BEGIN`/`END` wrap the function table.
- [ ] Functions validate their arguments and never throw.
- [ ] Large payloads use shared memory, not base64.
- [ ] The module name matches the file name (`files.cpp` → `files`).
- [ ] You added the module to a manifest if it is part of the framework (not
      needed for app modules under `native/`).

## Next steps

- [Filesystem](filesystem.md) — study the stock `fs` module for patterns.
- [C++ API reference](../reference/cpp-api.md).
