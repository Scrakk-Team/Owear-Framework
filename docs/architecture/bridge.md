---
title: Bridge
description: The bridge is the kernel-injected script that gives every document window.ow,
order: 1
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Bridge

The bridge is the kernel-injected script that gives every document `window.ow`,
and the transport that carries calls and events between the renderer and the
kernel.

## Renderer ↔ kernel transport

The bridge auto-detects the WebView backend (`src/Bridge/BridgeScript.cpp`):

| Platform | JS → native | native → JS |
|---|---|---|
| Linux (WebKitGTK) | `window.webkit.messageHandlers.ow.postMessage` | `evaluate_javascript` |
| Windows (WebView2) | `window.chrome.webview.postMessage` | `ExecuteScript` |

## Wire format (text frames)

```jsonc
// invoke (JS → native)
{ "t": "invoke", "id": N, "m": "fs", "f": "readText", "a": ["/etc/x"], "w": windowId }

// result (native → JS, via __ow._apply(id, ok, '<json>'))
{ "t": "result", "id": N, "ok": true, "r": <json>, "b": "<base64>" }

// event (native → JS, via __ow._event(w, '<name>', '<json>'))
{ "t": "event", "w": windowId, "n": "resize", "p": <json> }
```

Payloads are injected as escaped JS literals (`json::JsLiteral`) resolved with
`JSON.parse` inside the document, so there are no double serializations.

## Batching

All `_apply`/`_event` calls produced in a tick are emitted in **one** `eval` per
tick (the "outbox" batching, F3). This keeps many small results from causing many
script evaluations.

## Large binaries

`fs.readFile` returns `{ __ow_shm: { id, size } }` for files ≥ 256 KB. The
renderer reads it with `ow.readShared(handle)`, which fetches an `ArrayBuffer`
served from the mmap through the `ow-shm://` scheme. No base64, no extra kernel
copy. Verified with a 5 MB file whose SHA-256 matches on both sides.

## Synchronous calls

`ow.invokeSync` uses a blocking XHR to `ow-sync://`. It exists for bootstrap
only; the guide documents the reentrancy warning.

## Close veto

`closeRequested` carries `{ requestId }`. The renderer (or SDK) replies with
`ow-window.respondCloseRequest` / `win.closeRespond`. Without an answer within
`OW_CLOSE_TIMEOUT_MS` (default 1000 ms) the kernel closes anyway.

## Sidecar ↔ kernel (control socket)

The Node SDK speaks NDJSON over a Unix domain socket
(`$XDG_RUNTIME_DIR/owear-<pid>.sock`) or a named pipe (`\\.\pipe\owear-<pid>`):

```jsonc
{"id":1,"cmd":"window.create","params":{…}}
{"id":1,"ok":true,"result":{"windowId":1}}
{"id":2,"ok":false,"error":"window not found"}
{"event":"window.event","params":{"windowId":1,"name":"resize","payload":{…}}}
```

See the [control protocol](../reference/control-protocol.md) for every command.

## Module ABI

Native modules export `ow_module_descriptor()` (see
[Native module ABI](native-module-abi.md)). Memory contract: response buffers
live only during the call; the host copies immediately. No exceptions across the
ABI.

## Related

- [Renderer API](../api/renderer.md)
- [Control protocol](../reference/control-protocol.md)
