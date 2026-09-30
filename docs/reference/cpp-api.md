---
title: C++ API (include/ow/)
description: The public C++ headers declare the contracts shared by all supported platforms.
order: 3
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# C++ API (`include/ow/`)

The public C++ headers declare the contracts shared by all supported platforms.
Changing a signature here intentionally breaks compilation everywhere — that is
the anti-drift mechanism (see [Architecture rules](../contributing/architecture-rules.md)).

Most app developers only need `ow/Module.h` (for `.owm` modules) and possibly
`ow/Window.h` (for native-first apps). The rest is relevant to kernel
contributors.

## `ow/Module.h` — write a `.owm`

```cpp
#include <ow/Module.h>
#include <ow/Json.h>

static void readFile(const ow_request_t* req, ow_response_t* res) {
    // parse req->json, do work, respond
    ow::Module::RespondOk(res, "\"ok\"");
}

OW_MODULE_BEGIN(files, "1.0.0")
OW_FN(readFile)
OW_MODULE_END()
```

The macros build the function table and export `ow_module_descriptor()`. Helpers:

```cpp
ow::Module::RespondOk(ow_response_t* res, std::string_view json,
                      const uint8_t* bin = nullptr, uint32_t binLen = 0);
ow::Module::RespondError(ow_response_t* res, std::string_view message);
```

Memory contract: buffers live only during the call; the host copies immediately.
Never throw across the ABI. See [Native modules](../guides/native-modules.md) and
[the ABI reference](../architecture/native-module-abi.md).

## `ow/App.h` — native-first applications

```cpp
#include <ow/App.h>

ow::App::Main(argc, argv, ow::AppOptions{ .id, .name, .version });
ow::App::OnReady(fn);    // create windows here
ow::App::Post(fn);       // thread-safe callback on the main loop
ow::App::Quit(exitCode);
ow::App::Options();
```

`App::Main` initializes the platform, registers builtins, and runs the main loop
until `Quit`.

## `ow/Window.h` — native windows

```cpp
#include <ow/Window.h>

ow::Window w(ow::WindowOptions{
  .title = "App", .width = 1024, .height = 768,
  .resizable = true, .center = true, .frameless = false,
  .titleBarStyle = ow::TitleBarStyle::Custom,
  .titleBarOverlay = { .enabled = true, .height = 32 },
  .url = "app://index.html",
});
```

Key members:

```cpp
w.Id(); w.Show(); w.Hide(); w.Close(); w.Destroy(); w.Focus();
w.Minimize(); w.Maximize(); w.Unmaximize(); w.Restore();
w.SetFullScreen(bool); w.IsMaximized/IsMinimized/IsFullScreen();
w.GetBounds() / w.SetBounds(bounds); w.Center();
w.SetTitle(s) / w.Title();
w.SetTitleBarStyle(style) / w.SetTitleBarOverlay(overlay);
w.SetAlwaysOnTop(bool, level); w.SetSkipTaskbar(bool); w.SetKiosk(bool);
w.SetIgnoreMouseEvents(bool, bool forward); w.SetProgressBar(double, mode);
w.LoadURL(url);
w.EvalJS(js, cb(resultJson));
w.EmitToJS(name, jsonPayload);
w.On(name, fn(payload)) -> ListenerId; w.Off(id);
w.NativeHandle();               // GtkWidget* / HWND
w.CreateWebview(optionsJson);   // embedded child webviews
w.WebviewCommand(id, op, argsJson);
```

`WindowOptions` also includes `session`, `webviewArgs`, `parent`/`modal`,
`aspectRatio`, and the extended state booleans. See the header for the full set.

## `ow/Shm.h` — shared memory (C)

```cpp
const char*    ow_shm_put(const uint8_t* data, size_t len);
const uint8_t* ow_shm_data(const char* id, size_t* len);
void           ow_shm_shutdown(void);
```

Used to publish payloads ≥ 256 KB so the renderer can read them via
`ow.readShared` without base64. The host exports these symbols (`-rdynamic` on
Linux, an import library on Windows).

## `ow/Json.h` — minimal JSON

```cpp
auto parsed = ow::json::Parse(std::string_view(text));  // parsed.value is optional-like
ow::json::Value v;               // Null/Bool/Int/Double/String/Array/Object
v.IsArray(); v.AsArray(); v.Find("key"); v.AsObject();
v.Serialize();                   // to JSON text
ow::json::JsLiteral();           // escape as a JS literal for injection
```

## `ow/Base64.h`

```cpp
std::string ow::b64::Encode(std::string_view data);
bool        ow::b64::Decode(std::string_view text, std::string& out);
bool        ow::b64::Decode(std::string_view text, std::vector<uint8_t>& out);
```

## `ow/Common.h`

`Result<T>`, `OwBytes`, `WindowId`, `NonCopyable`, and other shared types.

## `ow/Menu.hpp`

`ow::menu::Item`, `ow::menu::ParseItems`, and `ow::menu::ClickPayload` — the
internal representation used by the `menu` and `tray` modules.

## Kernel internals (contributors)

Declared under `include/ow/Bridge`, `include/ow/Modules`, `include/ow/Runtime`,
and implemented in `src/`:

- `bridge::Codec`, `Dispatcher`, `ModuleLoader`, `ControlServer`, `NodeManager`;
- `http`, `archive::ExtractTarGz`, `crypto::Sha256`, `shm::Put/Data`.
