---
title: theme / nativeTheme
description: Read and control the OS light/dark preference. nativeTheme is an alias of
order: 18
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `theme` / `nativeTheme`

Read and control the OS light/dark preference. `nativeTheme` is an alias of
`theme`.

```ts
import { nativeTheme, theme } from '@owear/core'   // same object
```

## API

```ts
interface ThemeInfo {
  dark: boolean
  source: 'system' | 'light' | 'dark'
  highContrast: boolean
  reducedTransparency: boolean
}

theme.get(): Promise<ThemeInfo>
theme.isDark(): Promise<boolean>
theme.setSource(source: 'system' | 'light' | 'dark'): Promise<ThemeInfo>
theme.watch(): Promise<void>
theme.unwatch(): Promise<void>
```

`setSource` forces the preference. It also applies the color scheme to the web
content where the platform allows it (Windows: WebView2 `PreferredColorScheme`;
Linux: the GTK prefer-dark setting).

## Changes

`watch()` starts the native watcher, which emits `theme.changed`:

```ts
// main process (raw channel)
;(app as any).__channel.on('theme.changed', (info: ThemeInfo) => {})

// renderer
ow.on('theme.changed', (info) => {
  document.documentElement.dataset.theme = info.dark ? 'dark' : 'light'
})
```

## Backends

- Linux: GTK `gtk-application-prefer-dark-theme`, then GNOME 42+
  `org.gnome.desktop.interface color-scheme`, then the GTK theme name.
- Windows: registry.

`highContrast` and `reducedTransparency` are reported when the OS exposes them
and default to `false` otherwise.
