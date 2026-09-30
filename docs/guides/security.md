---
title: Security
description: Owear gives your UI a direct, powerful bridge to native code. That is the point —
order: 21
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Security

Owear gives your UI a direct, powerful bridge to native code. That is the point —
and it is also the thing to reason about. This page collects the defaults and the
levers you have.

## Trust model

- The **renderer is your code**. `ow.invoke` can call any loaded module,
  including `fs`, `process`, and `shell`. Treat the bridge like Node's `fs` API:
  only your own documents should run in a window that exposes it.
- **Remote content is untrusted.** If you load third-party pages, do not let
  them reach native modules without a permission boundary. Prefer webviews
  (`webview.*`) or a restricted session for external content.

## Permissions

Web platform features (geolocation, notifications, camera, microphone, clipboard
read) are **denied by default**. Grant them explicitly:

```ts
import { session } from '@owear/core'

session.onPermissionRequest(({ permission, origin }) => {
  return origin === 'app://index.html' && permission === 'notifications'
})
```

See [Sessions and downloads](sessions-and-downloads.md).

## Opening external content

- `shell.openExternal` accepts only `http`, `https`, and `mailto`. Other schemes
  are refused, which prevents `file://`, `javascript:`, and custom-scheme
  launches from untrusted links.
- Control `window.open` / `target="_blank"` with a window-open handler:

```ts
win.webContents.setWindowOpenHandler(({ url }) => {
  if (url.startsWith('https://trusted.example')) return { action: 'allow' }
  return { action: 'deny' }
})
```

## Custom protocols

`app.protocol(name, { privileged })` can mark a scheme `secure`, `cors`,
`standard`, `stream`, or `fetch`-capable. Only mark schemes you fully control as
`secure`; doing so lets pages treat their origin as trustworthy.

The `serve` form maps a scheme to a directory on disk. The kernel validates paths
to prevent traversal outside the served root.

## Shipping content security

- Set a Content-Security-Policy in your HTML. Owear does not inject one for you.
- Prefer `app://` over `file://` for bundled assets; `file://` has weaker origin
  semantics.
- Do not expose `node/call` handlers that perform sensitive actions without
  validating arguments. A handler runs whatever the renderer asks.

## Secrets

Store credentials with [`safeStorage`](safe-storage.md), never in
`localStorage` or plain files.

## Supply chain

- The updater verifies **sha256** always, and **Ed25519 signatures** when a
  public key is configured. Configure a public key for any distributed app; see
  [Auto-update](auto-update.md).
- Installer payloads can carry per-group protection flags (`integrity`,
  `readonly`, `hidden`, `signed`); see [Installers](installers.md).

## Checklist

- [ ] `session.onPermissionRequest` grants only what you need.
- [ ] `setWindowOpenHandler` denies by default.
- [ ] A CSP is set in `index.html`.
- [ ] Secrets use `safeStorage`.
- [ ] The updater has a public key configured.
- [ ] Remote content is isolated (separate webview/session).

## Next steps

- [Sessions and downloads](sessions-and-downloads.md).
- [Auto-update](auto-update.md).
