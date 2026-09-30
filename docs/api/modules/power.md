---
title: power
description: Battery, idle, session lifecycle (suspend/shutdown/lock), and sleep inhibitors.
order: 15
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `power`

Battery, idle, session lifecycle (suspend/shutdown/lock), and sleep inhibitors.

- **Kind:** module (`.owm`)
- **Version:** 0.2.0
- **Platforms:** Linux, Windows

## Functions

| Function | Signature |
|---|---|
| `monitorStart` | `() → null` |
| `monitorStop` | `() → null` |
| `idleTime` | `() → number` (seconds) |
| `idleState` | `(thresholdSeconds) → 'active' \| 'idle' \| 'locked' \| 'unknown'` |
| `isOnBattery` | `() → boolean` |
| `inhibitStart` | `(type?) → inhibitId` |
| `inhibitStop` | `(inhibitId) → boolean` |

## Events

After `monitorStart`:

| Event | Meaning |
|---|---|
| `power.suspend` | Going to sleep |
| `power.resume` | Woke up |
| `power.shutdown` | System is shutting down |
| `power.lock` | Session locked |
| `power.unlock` | Session unlocked |
| `power.ac` | Switched to AC power |
| `power.battery` | Switched to battery |

## Inhibitors

```ts
const id = await ow.invoke<number>('power', 'inhibitStart', 'prevent-display-sleep')
await ow.invoke('power', 'inhibitStop', id)
```

`type` defaults to `prevent-display-sleep`. On Linux v1 always uses the
ScreenSaver inhibitor regardless of type.

## Platform backends (Linux)

- `logind` for suspend/shutdown and session lock/unlock;
- UPower for battery, with a `/sys/class/power_supply` fallback;
- idle via X11 MIT-SCREEN-SAVER (`libXss`, `dlopen`ed); Wayland reports
  `unknown`/`0`;
- inhibitors via `org.freedesktop.ScreenSaver`.

Windows: `PowerBroadcast`/`WTS`/`GetLastInputInfo` (verify in CI).

## See also

- [Screen and power guide](../../guides/screen-and-power.md)
- [`powerMonitor` SDK](../../api/sdk/power-monitor.md)
