---
title: screen
description: Multi-monitor information and cursor position. screen is an EventEmitter that
order: 16
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `screen`

Multi-monitor information and cursor position. `screen` is an EventEmitter that
auto-connects the native watcher on first use.

```ts
import { screen } from '@owear/core'
```

## Methods

```ts
screen.watch(): Promise<void>                          // connect the native watcher
screen.getAllDisplays(): Promise<Display[]>
screen.getPrimaryDisplay(): Promise<Display | null>
screen.getCursorScreenPoint(): Promise<{ x: number; y: number }>
screen.getDisplayNearestPoint(point): Promise<Display | null>
screen.getDisplayMatching(rect): Promise<Display | null>
screen.screenToDipPoint(point): Promise<{ x: number; y: number }>
screen.dipToScreenPoint(point): Promise<{ x: number; y: number }>
```

`getDisplayNearestPoint` and `getDisplayMatching` are computed in the SDK from
the display bounds (the nearest rectangle, or the largest overlap; falling back
to the nearest display to the rectangle's center).

`screenToDipPoint`/`dipToScreenPoint` approximate the conversion using the
display's `scaleFactor` (exact when the scale factor is 1).

### Electron-style aliases

```ts
screen.getDisplays === getAllDisplays
screen.getPrimary === getPrimaryDisplay
screen.getCursor === getCursorScreenPoint
screen.nearestDisplay === getDisplayNearestPoint
screen.displayMatching === getDisplayMatching
```

## `Display`

```ts
interface Display {
  id: number
  primary: boolean
  label: string
  bounds: { x; y; width; height }        // native pixels
  size: { width; height }
  workArea: { x; y; width; height }
  workAreaSize: { width; height }
  scaleFactor: number
  rotation: number
  internal: boolean
  detected: boolean
  displayFrequency: number
  colorDepth: number
  depthPerComponent: number
  colorSpace: string
  monochrome: boolean
  touchSupport: 'available' | 'unavailable' | 'unknown'
  accelerometerSupport: 'available' | 'unavailable' | 'unknown'
  nativeOrigin: { x: number; y: number }
}
```

`id` is stable within a session: it is assigned the first time a monitor is seen
and kept until it disappears, so `added`/`removed`/`changed` refer to the same
display across hot-plug.

## Events

```ts
screen.on('added', (display: Display) => {})
screen.on('removed', (display: Display) => {})
screen.on('changed', (display: Display, metrics: DisplayMetric[]) => {})
```

`DisplayMetric` is `'bounds' | 'workArea' | 'scaleFactor' | 'rotation'`. The same
events reach the renderer as `screen.added`, `screen.removed`, `screen.changed`.
