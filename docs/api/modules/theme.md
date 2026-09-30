---
title: theme
description: System light/dark preference, forcing, and change events.
order: 21
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `theme`

System light/dark preference, forcing, and change events.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

## Functions

| Function | Signature |
|---|---|
| `get` | `() → { dark, source, highContrast, reducedTransparency }` |
| `isDark` | `() → boolean` |
| `setSource` | `('system' \| 'light' \| 'dark') → { dark, source, highContrast, reducedTransparency }` |
| `watch` | `() → null` |
| `unwatch` | `() → null` |

## Events

After `watch()`, changes emit `theme.changed` with the same payload as `get`:

```ts
{ dark: boolean, source: 'system' | 'light' | 'dark', highContrast: boolean, reducedTransparency: boolean }
```

`setSource` forces the preference and applies the color scheme to web content
where the platform allows it.

## Platform notes

- Linux: GTK `gtk-application-prefer-dark-theme`, then GNOME 42+
  `org.gnome.desktop.interface color-scheme`, then the GTK theme name.
- Windows: registry.
- `highContrast`/`reducedTransparency` default to `false` when unknown.

## See also

- [Theme guide](../../guides/theme.md)
- [`theme` SDK](../../api/sdk/theme.md)
