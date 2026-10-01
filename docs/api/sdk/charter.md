---
title: Charter (capability policy)
description: Declare what a window's own document may reach in the kernel, enforced per call.
order: 12
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Charter

The charter is the **capability policy of a window's own document**. It says
what the renderer may reach in the kernel, and the kernel refuses everything
else. It plays the role Electron gives to a hand-written preload (but enforced
by the runtime, not by convention) and the role Tauri gives to a capabilities
file (but declared in TypeScript, applied per window and revocable).

```ts
import { app, BrowserWindow, charterPresets, onCharterDenied } from '@owear/core'

app.whenReady().then(() => {
  const win = new BrowserWindow({
    charter: { allow: ['ow-window', 'theme', 'fs:readText'] },
  })
})
```

Once a window has a charter, **everything it does not grant is refused**. No
charter means no filtering, so this is opt-in per window and cannot break an
app that does not use it.

## Declaring

```ts
type CharterEntry = string // 'fs' | 'fs:readText' | 'fs:*' | '*'

interface Charter {
  allow?: CharterEntry | CharterEntry[]
  deny?: CharterEntry | CharterEntry[]
  enforce?: boolean // default: true when any rule is declared
}
```

- `allow` — the capabilities the window may use.
- `deny` — explicit refusals. **`deny` always wins over `allow`**, so you can
  grant a whole module and carve out single functions.
- `enforce: false` stores the rules but pauses them (handy while developing).

## Applying at runtime

```ts
await win.setCharter({ allow: ['ow-window', 'net'] })  // install / replace
await win.getCharter()                                 // { enforce, allow, deny }
await win.clearCharter()                               // back to the default
```

Capabilities can also be dropped mid-session, which is what makes the charter
different from a static config file:

```ts
await win.setCharter({ allow: ['ow-window', 'fs:readText'] })
// … later, after the untrusted flow is over:
await win.setCharter({ allow: ['ow-window'] })
```

## Presets

```ts
charterPresets.shell       // ['ow-window', 'theme'] — custom title bar + drags
charterPresets.readOnlyFs  // ['fs:readText', 'fs:readFile', 'fs:readDir', …]
charterPresets.node        // ['node:call'] — reach app.handle
```

## What happens when a call is refused

The renderer's promise rejects exactly like a module error would:

```ts
try {
  await ow.invoke('fs', 'writeText', ['/etc/passwd', 'x'])
} catch (e) {
  e.message // "charter: capability 'fs:writeText' is not granted to this window"
}
```

The main process also receives an audit event, so a refusal can be logged,
surfaced in the UI, or turned into a CI failure:

```ts
onCharterDenied(({ windowId, capability, module, fn }) => {
  console.warn(`default-deny: ${capability} from window ${windowId}`)
})
```

## Scope of the enforcement

The check happens on the **renderer** paths only:

| Path | Where |
|---|---|
| `ow.invoke` via `postMessage` | `Window/Common/Messages.cpp` |
| `ow.invoke` via `ow-rpc://` (`fetch`) | `Webview/linux/Backend/Rpc.cpp` |
| `ow.invokeSync` via `ow-sync://` (XHR) | `Webview/linux/Backend/Protocols.cpp` |
| WebView2 message handler (Windows) | `Webview/win/Backend/Handlers.cpp` |

The **main process is trusted and never filtered**, so `module.invoke` from
`@owear/core`, the installer API and the updater keep working with a charter
installed.

Two structural properties complete the picture:

- **The bridge only exists in the top frame.** Subframes (extension panels,
  embedded pages, remote iframes) cannot reach the kernel at all.
- **Embedded webviews are unprivileged by construction.** The `webview` module
  never injects the bridge into its child views.

## Cost

Measured in `benchmarks/charter/RESULTS.md` on the same machine as the main
suite: a **granted** call adds ~**1.5%** over no charter at all (a lookup), and
a **refused** one ~**10.5%**, because every refusal also emits a
`charter.denied` audit event.

## See also

- [Security model](../../guides/security.md)
- [Renderer API](../renderer.md)
