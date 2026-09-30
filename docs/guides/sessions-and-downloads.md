---
title: Sessions and downloads
description: The session builtin manages the WebView's cookies, cache, proxy, and downloads.
order: 22
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Sessions and downloads

The `session` builtin manages the WebView's cookies, cache, proxy, and downloads.
The SDK adds permission handling and session partitions.

## Permissions

Decide at runtime whether the page may use geolocation, notifications, camera,
microphone, and so on. If no handler is registered, requests are **denied**.

```ts
import { session } from '@owear/core'

const off = session.onPermissionRequest(({ permission, origin }) => {
  return permission === 'notifications' && origin === 'https://myapp.local'
})
```

`permission` values include `geolocation`, `notifications`, `media`,
`microphone`, `camera`, `clipboardRead`, and others, matching the platform's
WebView vocabulary. Registering a handler tells the kernel to route requests.

## Partitions

Isolate cookies/storage/cache per profile (for example, multiple accounts):

```ts
const profile = session.fromPartition('persist:account-2')
const win = new BrowserWindow({ session: profile.partition, url })
```

On WebKit this maps to a separate data directory; on WebView2 to a separate
profile. An empty partition uses the app default.

## Cookies

From the renderer or main (via `invokeNative`):

```ts
const { cookies } = await ow.invoke<{ cookies: Cookie[] }>(
  'session', 'cookiesGet', 'https://example.com',
)
// optionally filter by name/domain
await ow.invoke('session', 'cookiesGet', 'https://example.com', 'session_id', 'example.com')

await ow.invoke('session', 'cookiesSet', {
  name: 'session_id',
  value: 'abc',
  domain: 'example.com',
  path: '/',
  maxAge: 3600,
})

const deleted = await ow.invoke<boolean>('session', 'cookieDelete', 'https://example.com', 'session_id')
```

Each cookie is `{ name, value, domain, path, httpOnly, secure }`.

## Cache and site data

```ts
await ow.invoke('session', 'clearStorage', windowId, { cookies: true, localStorage: true })
```

Cache (disk and memory) is always cleared; pass `cookies`/`localStorage` to clear
those too.

## Proxy

```ts
await ow.invoke('session', 'setProxy', windowId, 'http://proxy.local:8080')
await ow.invoke('session', 'setProxy', windowId, 'system')   // restore system default
```

## Downloads

Attach download tracking to a window, then listen for events:

```ts
await ow.invoke('session', 'attach', windowId)

ow.on('session.downloadStarted', ({ downloadId, destination }) => {})
ow.on('session.downloadProgress', ({ downloadId, progress }) => {})  // progress: 0..1
ow.on('session.downloadFinished', ({ downloadId }) => {})

await ow.invoke('session', 'downloadCancel', downloadId)
```

## User agent and spell check

```ts
await ow.invoke('session', 'setUserAgentAll', 'MyApp/1.0')
await ow.invoke('session', 'setUserAgent', windowId, 'MyApp/1.0')   // per window

await ow.invoke('session', 'spellCheck', windowId, 'es', 'en')      // enable
await ow.invoke('session', 'spellCheck', windowId)                  // disable
```

## Request interception

`webRequest.onBeforeRequest` can cancel or redirect requests. On **Windows** it
intercepts every request; on **Linux** only main-frame navigations (WebKitGTK
2.52 no longer exposes `send-request` for subresources).

```ts
import { webRequest } from '@owear/core'

const off = webRequest.onBeforeRequest(
  { urls: ['https://ads.example.com/*'] },
  () => ({ cancel: true }),
)

// or redirect
webRequest.onBeforeRequest('<all_urls>', (details) =>
  details.url.startsWith('http://') ? { redirectURL: details.url.replace('http://', 'https://') } : undefined,
)
```

Patterns are globs (`*`, `?`) or the special `<all_urls>` / `*`.

## Next steps

- [`session` module reference](../api/modules/session.md).
- [Security](security.md) — permissions and protocol hardening.
