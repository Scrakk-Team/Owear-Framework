---
title: Architecture rules
description: Four rules explain most decisions in the repo. Breaking them either breaks the
order: 2
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Architecture rules

Four rules explain most decisions in the repo. Breaking them either breaks the
build or, worse, silently breaks a platform. Read this before changing shared
code.

## 1. Anti-drift: public contracts live in `include/ow/`

Those headers are included by all supported platforms and by user modules. If you
change a signature, the change breaks compilation — which is exactly what we want.
It is a feature, not a nuisance.

## 2. One implementation per platform, chosen by CMake — never `#ifdef`

`src/**/*_linux.cpp` and `*_win.cpp` are selected in the corresponding
`CMakeLists.txt`. Do **not** put platform branches inside a common file.

```text
window_linux.cpp   window_win.cpp   ← chosen by CMake
```

`macro`s or `#ifdef` inside a shared file defeat the anti-drift design and hide
which platforms you actually compile.

## 3. All UI work goes through the main loop, via `App::Post`

That is what makes it safe for a module with its own thread to touch windows.
`App::Post` is thread-safe; the rest of the window API is not.

## 4. The module ABI (`ow_api.h`) is C, with an explicit memory contract

`ow_response_t` buffers live **only during the call**: the host copies them
immediately. Never throw across the ABI. See
[Native module ABI](../architecture/native-module-abi.md).

## Working habits that follow from these rules

- Declare functionality in a module's descriptor table, not in an `if` on the
  function name.
- Before "fixing" something that looks broken, check whether a test covers it and
  add the missing one if not. The project has had live bugs behind a green suite.
- Update the `VERIFY ON REAL DESKTOP` label with the OS version you tested on;
  an unmaintained label is worthless.

## Related

- [Kernel and lifecycle](../architecture/kernel.md)
- [Contributing setup](setup.md)
- [Adding an API](adding-an-api.md)
