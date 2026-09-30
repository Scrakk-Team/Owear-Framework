---
title: session
description: WebView session management  cookies, cache, proxy, downloads, user agent, spell
order: 19
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `session`

WebView session management: cookies, cache, proxy, downloads, user agent, spell
check.

- **Kind:** builtin
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

## Functions

| Function | Signature |
|---|---|
| `cookiesGet` | `(url, name?, domain?) → { cookies: Cookie[] }` |
| `cookiesSet` | `({ name, value, domain, path?, maxAge? }) → null` |
| `cookieDelete` | `(url, name) → boolean` |
| `clearStorage` | `(windowId, { cookies?, localStorage? }) → null` |
| `setProxy` | `(windowId, proxyUrl \| 'system') → null` |
| `attach` | `(windowId) → null` |
| `downloadCancel` | `(downloadId) → null` |
| `setUserAgentAll` | `(ua) → null` |
| `spellCheck` | `(windowId, ...langs) → null` |

## Cookies

```ts
// Cookie = { name, value, domain, path, httpOnly, secure }
const { cookies } = await ow.invoke('session', 'cookiesGet', 'https://example.com')
await ow.invoke('session', 'cookiesSet', { name: 'sid', value: 'abc', domain: 'example.com', maxAge: 3600 })
const deleted = await ow.invoke<boolean>('session', 'cookieDelete', 'https://example.com', 'sid')
```

`cookiesGet` accepts optional `name` and `domain` filters.

## Cache and site data

```ts
await ow.invoke('session', 'clearStorage', windowId, { cookies: true, localStorage: true })
```

Disk and memory cache are always cleared; `cookies`/`localStorage` clear those
too.

## Proxy

```ts
await ow.invoke('session', 'setProxy', windowId, 'http://proxy.local:8080')
await ow.invoke('session', 'setProxy', windowId, 'system')
```

## Downloads

`attach(windowId)` connects download tracking; then:

| Event | Payload |
|---|---|
| `session.downloadStarted` | `{ downloadId, destination }` |
| `session.downloadProgress` | `{ downloadId, progress }` (0..1) |
| `session.downloadFinished` | `{ downloadId }` |

```ts
await ow.invoke('session', 'attach', windowId)
ow.on('session.downloadProgress', ({ downloadId, progress }) => update(downloadId, progress))
await ow.invoke('session', 'downloadCancel', downloadId)
```

## User agent and spell check

```ts
await ow.invoke('session', 'setUserAgentAll', 'MyApp/1.0')
await ow.invoke('session', 'spellCheck', windowId, 'es', 'en')   // enable
await ow.invoke('session', 'spellCheck', windowId)               // disable
```

## Notes

- The builtin runs on the GTK main thread and bridges async WebKit calls with
  nested main loops.
- Permissions and partitions are handled by the [SDK `session`](../../api/sdk/session.md).
- Request interception is [webRequest](../../api/sdk/web-request.md).

## See also

- [Sessions and downloads guide](../../guides/sessions-and-downloads.md)
