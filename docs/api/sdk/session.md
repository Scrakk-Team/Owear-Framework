---
title: session
description: Permissions and session partitions. Cookie/cache/proxy/download operations live
order: 17
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `session`

Permissions and session partitions. Cookie/cache/proxy/download operations live
in the native [`session` module](../../api/modules/session.md); this SDK page
covers the JavaScript-facing part.

```ts
import { session } from '@owear/core'
```

## Permissions

```ts
interface PermissionRequest {
  id: number
  permission: string    // 'geolocation' | 'notifications' | 'media' | 'microphone' | 'camera' | 'clipboardRead' | …
  origin: string
}

type PermissionHandler = (req: PermissionRequest) => boolean | Promise<boolean>

session.onPermissionRequest(handler: PermissionHandler): () => void
```

Registers a handler and tells the kernel to route permission requests. Multiple
handlers may be registered; a request is **allowed if any handler returns
truthy**. With no handler, everything is denied.

```ts
const off = session.onPermissionRequest(({ permission, origin }) =>
  permission === 'notifications' && origin === 'app://index.html',
)
```

## Partitions

```ts
session.fromPartition(partition: string): { partition: string }
```

Returns a handle whose `partition` string you pass to a window:

```ts
const profile = session.fromPartition('persist:account-2')
new BrowserWindow({ session: profile.partition, url })
```

Use `persist:` to make the partition durable. Omitting `session` uses the app's
default profile.

## Related

- Cookies, cache, proxy, downloads, user agent, spell check:
  [`session` module](../../api/modules/session.md).
- Request interception: [web-request.md](web-request.md).
