---
title: powerMonitor and powerSaveBlocker
description: Energy awareness in the main process.
order: 13
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `powerMonitor` and `powerSaveBlocker`

Energy awareness in the main process.

```ts
import { powerMonitor, powerSaveBlocker } from '@owear/core'
```

## `powerMonitor`

An EventEmitter that auto-connects the native monitor on first use.

```ts
powerMonitor.watch(): Promise<void>
powerMonitor.getIdleTime(): Promise<number>                 // seconds
powerMonitor.getIdleState(threshold: number): Promise<IdleState>
powerMonitor.isOnBatteryPower(): Promise<boolean>
powerMonitor.onBatteryPower: boolean                        // last known value
```

```ts
type IdleState = 'active' | 'idle' | 'locked' | 'unknown'
```

Electron-style aliases: `powerMonitor.getSystemIdleTime` = `getIdleTime`,
`powerMonitor.getSystemIdleState` = `getIdleState`.

### Events

```ts
powerMonitor.on('suspend', () => {})
powerMonitor.on('resume', () => {})
powerMonitor.on('shutdown', () => {})
powerMonitor.on('lock', () => {})
powerMonitor.on('unlock', () => {})
powerMonitor.on('ac', () => {})
powerMonitor.on('battery', () => {})
```

In the renderer these arrive as `power.suspend`, `power.resume`, `power.shutdown`,
`power.lock`, `power.unlock`, `power.ac`, `power.battery`.

## `powerSaveBlocker`

```ts
type PowerSaveBlockerType = 'prevent-app-suspension' | 'prevent-display-sleep'

powerSaveBlocker.start(type?: PowerSaveBlockerType): number
powerSaveBlocker.stop(id: number): boolean
powerSaveBlocker.isStarted(id: number): boolean
```

`prevent-display-sleep` takes precedence over `prevent-app-suspension`. The method
returns synchronously with a client id; the native inhibit is started in the
background once the app is ready.

```ts
const id = powerSaveBlocker.start('prevent-display-sleep')
// … long export …
powerSaveBlocker.stop(id)
```

## Platform backends (Linux)

- suspend/resume/shutdown, lock/unlock: `logind`;
- battery: UPower with a `/sys/class/power_supply` fallback;
- idle: X11 MIT-SCREEN-SAVER (`libXss`, `dlopen`ed); Wayland reports
  `unknown`/`0`;
- inhibitors: `org.freedesktop.ScreenSaver`.

Windows: `PowerBroadcast`/`WTS`/`GetLastInputInfo` (verify in CI).
