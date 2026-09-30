---
title: screen
description: Monitors, primary display, cursor position, and change events.
order: 18
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `screen`

Monitors, primary display, cursor position, and change events.

- **Kind:** module (`.owm`)
- **Version:** 0.2.0
- **Platforms:** Linux, Windows

## Functions

| Function | Signature |
|---|---|
| `getAllDisplays` | `() → Display[]` |
| `getPrimaryDisplay` | `() → Display \| null` |
| `getCursorScreenPoint` | `() → { x, y }` |
| `watch` | `() → null` |
| `unwatch` | `() → null` |

## `Display`

```ts
{
  id: number, primary: boolean, label: string,
  bounds: { x, y, width, height },
  size: { width, height },
  workArea: { x, y, width, height },
  workAreaSize: { width, height },
  scaleFactor: number, rotation: number,
  internal: boolean, detected: boolean,
  displayFrequency: number, colorDepth: number,
  depthPerComponent: number, colorSpace: string, monochrome: boolean,
  touchSupport: 'available' | 'unavailable' | 'unknown',
  accelerometerSupport: 'available' | 'unavailable' | 'unknown',
  nativeOrigin: { x, y }
}
```

`id` is stable within a session: assigned on first sighting of a monitor and kept
until it disappears, so events can refer to the same display across hot-plug.

## Events

After `watch()`:

| Event | Payload |
|---|---|
| `screen.added` | `{ display }` |
| `screen.removed` | `{ display }` |
| `screen.changed` | `{ display, metrics: string[] }` |

`metrics` values: `bounds`, `workArea`, `scaleFactor`, `rotation`.

```ts
await ow.invoke('screen', 'watch')
ow.on('screen.changed', ({ display, metrics }) => console.log(display.id, metrics))
```

## Notes

- Linux uses GDK; fields not exposed by GDK3 (rotation, `internal`) use honest
  defaults.
- The SDK's `screen` object computes `nearest`/`matching` in JS and adds
  DIP/screen coordinate helpers.

## See also

- [Screen and power guide](../../guides/screen-and-power.md)
- [`screen` SDK](../../api/sdk/screen.md)
