---
title: Theme
description: The theme module reads the OS light/dark preference, can force a preference,
order: 23
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Theme

The `theme` module reads the OS light/dark preference, can force a preference,
and emits `theme.changed`. The SDK exposes it as `theme` / `nativeTheme`.

```ts
import { nativeTheme } from '@owear/core'

const info = await nativeTheme.get()
// { dark: boolean, source: 'system'|'light'|'dark', highContrast: boolean, reducedTransparency: boolean }

await nativeTheme.isDark()

// force a preference (also applied to web content where the OS supports it)
await nativeTheme.setSource('dark')   // 'system' | 'light' | 'dark'

await nativeTheme.watch()             // start emitting changes
await nativeTheme.unwatch()
```

## Reacting to changes

```ts
// main process
nativeTheme.watch()
ow /* not available in main */ // use the SDK channel:
```

In the renderer, subscribe directly:

```ts
ow.on('theme.changed', (info) => {
  document.documentElement.dataset.theme = info.dark ? 'dark' : 'light'
})
```

In the main process, `setSource` already forces the color scheme on the content
(Windows: WebView2 `PreferredColorScheme`; Linux: best effort). To react in the
main process, listen on the raw channel:

```ts
import { app } from '@owear/core'
app.whenReady().then(() => {
  ;(app as any).__channel.on('theme.changed', (info: { dark: boolean }) => {
    console.log('theme changed', info)
  })
  void nativeTheme.watch()
})
```

## How detection works per platform

- **Linux:** GTK `gtk-application-prefer-dark-theme`, then GNOME 42+
  `org.gnome.desktop.interface color-scheme`, then the GTK theme name.
- **Windows:** the registry.

`highContrast` and `reducedTransparency` are reported honestly where the OS
exposes them and default to `false` otherwise.

## Next steps

- [`theme` module reference](../api/modules/theme.md).
- [SDK `theme` reference](../api/sdk/theme.md).
