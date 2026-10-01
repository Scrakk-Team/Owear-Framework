---
title: 0.1.7
description: The charter — a kernel-enforced capability policy for the renderer, plus a tighter default for the injected bridge.
order: 7
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# 0.1.7

This release adds the **charter**: the capability policy of a window's own
document, enforced by the kernel. It is the middle ground between Electron
(where whatever the preload exposes is reachable by anything in the document)
and Tauri (where capabilities are a static config file): the charter is
**declared in TypeScript, applied per window at runtime and revocable**.

## The charter

- **`window.setCharter` / `getCharter` / `clearCharter`** (control socket) and
  the SDK surface on `BrowserWindow`:
  ```ts
  const win = new BrowserWindow({ charter: { allow: ['ow-window', 'fs:readText'] } })
  await win.setCharter({ allow: [charterPresets.shell, 'net'], deny: ['net:download'] })
  await win.clearCharter()
  ```
- **Deny by default.** Once a window has a charter, every call the kernel did
  not grant is refused with
  `charter: capability '<module>:<fn>' is not granted to this window`.
  Without a charter the behaviour is unchanged (nothing is filtered).
- **`deny` always wins** over `allow`, which lets you grant a whole module and
  carve out single functions (`{ allow: ['fs:*'], deny: ['fs:writeText'] }`).
- **Entry grammar:** `module`, `module:fn`, `module:*` or `*`.
- **Runtime revocation:** capabilities can be dropped while the app runs; a
  destroyed window forgets its charter so ids cannot leak grants.
- **Audit trail:** every refusal broadcasts `charter.denied`
  (`{ windowId, capability, module, fn }`) to the main process.
  `onCharterDenied()` in the SDK subscribes to it.
- **Presets** (`charterPresets.shell`, `.readOnlyFs`, `.node`) for the common
  capability sets.

### Where it is enforced

Only the **renderer** paths are filtered — the main process (control socket) is
trusted and never filtered, so `module.invoke` from `@owear/core` keeps working:

- `src/Window/Common/Messages.cpp` — the WebView message path (`ow.invoke`).
- `src/Webview/linux/Backend/Protocols.cpp` — `ow-sync://` (blocking invoke).
- `src/Webview/linux/Backend/Rpc.cpp` — `ow-rpc://` (`fetch` invoke).
- `src/Webview/win/Backend/Handlers.cpp` — the same path on WebView2.

## Tighter default for the injected bridge

- **The privileged surface is top-frame only.** The bridge script is injected
  with `WEBKIT_USER_CONTENT_INJECT_TOP_FRAME` (Linux) and self-disables with
  `window.top !== window.self` (all platforms), so subframes — extension
  panels, embedded pages, remote iframes — no longer get `window.ow`.
  Note that the underlying message handler is registered per view: a subframe
  can still post raw messages into the kernel, so the **charter is the real
  boundary** (see `tests/e2e/charter_guard.py`).
- **The transport is captured at document-start.** `window.webkit` /
  `window.chrome.webview` and `JSON.stringify` are read once, in a closure,
  instead of being looked up on every call. Page scripts can no longer hook the
  channel and read or rewrite what the app sends.
- **Embedded webviews stay unprivileged by construction:** the bridge is only
  injected into the window's main view, never into the child webviews created
  by the `webview` module.

## Benchmarks

`benchmarks/charter/` measures what the policy costs on the same machine as the
main suite (`benchmarks/charter/RESULTS.md`):

| state | ops/s | µs/call | overhead |
|---|---:|---:|---:|
| off (no charter) | 2260 | 442.5 | — |
| charter: allow | 2226 | 449.2 | +1.5% |
| charter: deny | 2044 | 489.2 | +10.5% |

A granted call costs a lookup (**+1.5%**, inside noise); a refused one pays for
the audit event it emits (**+10.5%**).

## Tests

- `tests/e2e/charter_guard.py` — 12 assertions against a live kernel: default
  behaviour, charter installation, refusal message, granted calls, the audit
  event, `deny` winning over `allow`, and `clearCharter`.
- `packages/core/test/charter.test.mjs` — the SDK side (`defineCharter`
  normalization, presets, event subscription).
