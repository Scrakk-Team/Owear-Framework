---
title: path
description: Pure path math plus OS standard directories.
order: 14
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `path`

Pure path math plus OS standard directories.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

## Functions

| Function | Signature | Result |
|---|---|---|
| `join` | `(...segments) → string` | Segments joined with the platform separator |
| `resolve` | `(...segments) → string` | Absolute, normalized (an absolute segment resets) |
| `dirname` | `(path) → string` | Parent path |
| `basename` | `(path, ext?) → string` | Filename, optionally without `ext` |
| `extname` | `(path) → string` | Extension including the dot |
| `normalize` | `(path) → string` | Lexically normalized |
| `homeDir` | `() → string` | Home directory |
| `appDataDir` | `() → string` | App data directory |
| `userDataDir` | `() → string` | Per-user app data |
| `cacheDir` | `() → string` | Cache directory |
| `tempDir` | `() → string` | Temp directory |
| `configDir` | `() → string` | Config directory |
| `exeDir` | `() → string` | Directory of the executable |
| `cwd` | `() → string` | Current working directory |

## Notes

- `join` uses the C++ `std::filesystem::path` operator, so it follows the host
  separator convention.
- `resolve` starts from the current directory and treats any absolute segment as
  a restart.
- `basename(path, ext)` strips `ext` whether or not it includes the leading dot.
- The directory functions mirror the XDG conventions on Linux and the Known
  Folders on Windows. The SDK's `app.getPath(name)` exposes a broader, Electron-
  styled set; prefer whichever is nearer to your code.

## Example

```ts
const dir = await ow.invoke<string>('path', 'resolve', '~', 'notes')
const name = await ow.invoke<string>('path', 'basename', '/home/me/readme.md', '.md') // "readme"
const ext = await ow.invoke<string>('path', 'extname', 'archive.tar.gz')              // ".gz"
```

## See also

- [Filesystem guide](../../guides/filesystem.md)
- [`fs`](fs.md)
