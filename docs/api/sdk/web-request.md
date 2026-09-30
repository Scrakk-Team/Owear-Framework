---
title: webRequest
description: Intercept requests made by the WebView. Handlers can cancel or redirect.
order: 21
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `webRequest`

Intercept requests made by the WebView. Handlers can **cancel** or **redirect**.

```ts
import { webRequest } from '@owear/core'
```

## `onBeforeRequest`

```ts
webRequest.onBeforeRequest(
  filter: { urls?: string[] } | string[],
  handler: WebRequestHandler,
): () => void
```

```ts
interface WebRequestDetails {
  id: number
  url: string
  method: string
  headers: Record<string, string>
}

type WebRequestResult = { cancel?: boolean; redirectURL?: string } | void
type WebRequestHandler = (details: WebRequestDetails) => WebRequestResult | Promise<WebRequestResult>
```

If `filter` is an array it is used as the URL patterns; if it is an object,
`filter.urls` is used (defaulting to `['<all_urls>']`). The return value of
`onBeforeRequest` is an unsubscribe function.

## Patterns

URL patterns are simple globs: `*` matches any run of characters, `?` matches a
single character. `<all_urls>` and `*` match everything.

```ts
// block analytics
const off = webRequest.onBeforeRequest(
  { urls: ['https://analytics.example.com/*'] },
  () => ({ cancel: true }),
)

// upgrade http to https
webRequest.onBeforeRequest('<all_urls>', (d) =>
  d.url.startsWith('http://') ? { redirectURL: d.url.replace('http://', 'https://') } : undefined,
)
```

When several registrations match a request, they run in order; the first one that
returns `cancel` or `redirectURL` decides.

## Platform limitation

- **Windows:** intercepts every request.
- **Linux:** only main-frame navigations (WebKitGTK 2.52 no longer exposes
  `send-request` for subresources). Subresource interception is not available.

The registration is synced to the kernel lazily, on `app.whenReady()`.
