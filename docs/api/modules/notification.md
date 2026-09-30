---
title: notification
description: System notifications.
order: 13
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `notification`

System notifications.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

## Functions

| Function | Signature |
|---|---|
| `show` | `(title, body?, appName?) → notificationId` |

```ts
const id = await ow.invoke<number>('notification', 'show', 'My App', 'Job finished', 'My App')
```

- `appName` defaults to `"Owear"`.
- Returns the system notification id (on Linux, the freedesktop notification id).

## Platform notes

- Linux: `org.freedesktop.Notifications` over D-Bus. Without a notification
  daemon (dunst, GNOME Shell, …) it returns an error.
- Windows: toast (verify in CI). Icons and actions are not supported in v1.

## See also

- [Dialogs and notifications guide](../../guides/dialogs-and-notifications.md)
- [`dialog`](dialog.md)
