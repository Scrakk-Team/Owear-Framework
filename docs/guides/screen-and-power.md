---
title: Screen and power
description: Two modules cover environment awareness  screen (monitors and cursor) and
order: 20
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Screen and power

Two modules cover environment awareness: `screen` (monitors and cursor) and
`power` (battery, idle, suspend, inhibitors).

## Screen

```ts
import { screen } from '@owear/core'

screen.watch()                                  // connect the native watcher
const displays = await screen.getAllDisplays()  // Display[]
const primary   = await screen.getPrimaryDisplay()
const cursor    = await screen.getCursorScreenPoint()
const nearest   = await screen.getDisplayNearestPoint(cursor)
const matching  = await screen.getDisplayMatching({ x: 0, y: 0, width: 800, height: 600 })
```

A `Display` carries geometry, work area, scale, rotation, label, and more:

```ts
interface Display {
  id: number          // stable within a session
  primary: boolean
  label: string
  bounds: { x, y, width, height }      // native pixels
  size: { width, height }
  workArea: { x, y, width, height }
  workAreaSize: { width, height }
  scaleFactor: number
  rotation: number
  internal: boolean
  displayFrequency: number
  colorDepth: number
  colorSpace: string
  touchSupport: 'available' | 'unavailable' | 'unknown'
  nativeOrigin: { x, y }
}
```

Coordinate conversions:

```ts
await screen.screenToDipPoint({ x, y })
await screen.dipToScreenPoint({ x, y })
```

### Events

```ts
ow.on('screen.added',   ({ display }) => {})
ow.on('screen.removed', ({ display }) => {})
ow.on('screen.changed', ({ display, metrics }) => {})   // metrics: ['bounds'|'workArea'|'scaleFactor'|'rotation']
```

The SDK also emits these on `screen` itself (`screen.on('added', …)`), and
auto-connects the watcher on first use.

## Power

### `powerMonitor`

```ts
import { powerMonitor } from '@owear/core'

await powerMonitor.watch()
await powerMonitor.isOnBatteryPower()      // boolean
powerMonitor.onBatteryPower                // last known value
await powerMonitor.getIdleTime()           // seconds
await powerMonitor.getIdleState(30)        // 'active' | 'idle' | 'locked' | 'unknown'
```

Events:

```ts
powerMonitor.on('suspend', () => {})
powerMonitor.on('resume', () => {})
powerMonitor.on('shutdown', () => {})
powerMonitor.on('lock', () => {})
powerMonitor.on('unlock', () => {})
powerMonitor.on('ac', () => {})        // plugged in
powerMonitor.on('battery', () => {})   // on battery
```

In the renderer the same events arrive as `power.suspend`, `power.resume`,
`power.ac`, `power.battery`, `power.shutdown`, `power.lock`, `power.unlock`.

### `powerSaveBlocker`

Prevent the system from sleeping while doing long work:

```ts
const id = powerSaveBlocker.start('prevent-display-sleep')
// later
powerSaveBlocker.stop(id)
powerSaveBlocker.isStarted(id)
```

Types: `prevent-display-sleep` (stronger) and `prevent-app-suspension`.
`prevent-display-sleep` takes precedence.

### Platform backends (Linux)

- suspend/resume/shutdown and lock/unlock: `logind` D-Bus;
- battery: UPower, with a `/sys/class/power_supply` fallback;
- idle: X11 MIT-SCREEN-SAVER (`libXss`, loaded with `dlopen`); on Wayland
  idle is reported as `unknown`/`0`;
- inhibitors: `org.freedesktop.ScreenSaver`.

Windows uses `PowerBroadcast`/`WTS`/`GetLastInputInfo` (verify in CI).

## Next steps

- [`screen` module reference](../api/modules/screen.md) and [`power`](../api/modules/power.md).
- [SDK `screen`](../api/sdk/screen.md) and [`powerMonitor`](../api/sdk/power-monitor.md).
