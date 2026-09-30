---
title: Native module ABI
description: The module ABI is the stable C interface between the kernel and .owm shared
order: 3
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Native module ABI

The module ABI is the stable C interface between the kernel and `.owm` shared
libraries. It is deliberately minimal and explicit.

## Exported symbol

A module exports exactly one symbol:

```cpp
extern "C" const ow_module_desc_t* ow_module_descriptor(void);
```

The macros `OW_MODULE_BEGIN` / `OW_FN` / `OW_MODULE_END` generate it:

```cpp
OW_MODULE_BEGIN(files, "1.0.0")
OW_FN(readText)
OW_FN(writeText)
OW_MODULE_END()
```

## Types (`ow_api.h`)

```cpp
typedef void (*ow_fn_t)(const ow_request_t*, ow_response_t*);

typedef struct {
    const char* name;
    ow_fn_t fn;
} ow_fn_entry_t;

typedef struct {
    const char* name;
    const char* version;
    const ow_fn_entry_t* fns;
    uint32_t fn_count;
} ow_module_desc_t;

typedef struct ow_request_t {
    const char* json;       // JSON array of arguments
    uint32_t    json_len;
    const uint8_t* bin;
    uint32_t    bin_len;
    uint32_t    window_id;  // invoking window (0 if none)
    void*       host;       // opaque host handle
} ow_request_t;

typedef struct ow_response_t {
    int         status;     // 0 = ok, non-zero = error
    const char* error;
    const char* json;
    uint32_t    json_len;
    const uint8_t* bin;
    uint32_t    bin_len;
} ow_response_t;
```

## Optional host hook

Modules that emit events or log receive a host handle:

```cpp
extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h);
```

## Memory contract

- Buffers returned through `res` are valid **only during the call**. The host
  copies them immediately.
- `ow::Module::RespondOk` / `RespondError` use a thread-local buffer that lives
  until the next call on the same thread — safe under the contract.
- **Never** throw an exception across the ABI. Catch and return an error.

## Large payloads

Use shared memory (`ow_shm_put`) and return a `{ __ow_shm: { id, size } }`
handle instead of embedding base64. The host exports the `ow_shm_*` symbols:
`-rdynamic` (Linux) and an import library generated for the kernel executable
(MSVC `ENABLE_EXPORTS`).

## Versioning

The module version is a semver string in the descriptor. The kernel records it
in the registry alongside `origin` (the `.owm` path or `builtin:<name>`).

## Related

- [Native modules guide](../guides/native-modules.md)
- [C++ API](../reference/cpp-api.md)
- [Module manifests](../reference/manifests.md)
