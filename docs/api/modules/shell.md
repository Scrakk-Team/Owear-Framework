---
title: shell
description: Integration with the system's browser and file manager.
order: 20
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `shell`

Integration with the system's browser and file manager.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

## Functions

| Function | Signature |
|---|---|
| `openExternal` | `(url) → null` |
| `openPath` | `(path) → null` |
| `showItemInFolder` | `(path) → null` |

`openExternal` accepts only `http://`, `https://`, and `mailto:` URLs; anything
else is refused with an error. This prevents launching arbitrary schemes from
untrusted links.

## Example

```ts
await ow.invoke('shell', 'openExternal', 'https://owear.dev')
await ow.invoke('shell', 'openPath', '/home/me/report.pdf')
await ow.invoke('shell', 'showItemInFolder', '/home/me/report.pdf')
```

## Platform notes

- Linux: `GAppInfo`; `showItemInFolder` uses the `org.freedesktop.FileManager1`
  D-Bus interface with a fallback that opens the parent directory.
- Windows: `ShellExecute`/`IShellLink` (verify in CI).

## See also

- [Clipboard and shell guide](../../guides/clipboard-and-shell.md)
