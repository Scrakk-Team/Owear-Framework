---
title: fs
description: Filesystem module. Cross-platform C++ standard filesystem with per-platform
order: 7
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `fs`

Filesystem module. Cross-platform C++ standard filesystem with per-platform
extras for watchers and metadata.

- **Kind:** module (`.owm`)
- **Version:** 0.1.0
- **Platforms:** Linux, Windows

```ts
await ow.invoke('fs', 'readText', '/etc/hostname')
```

## Functions

| Function | Signature |
|---|---|
| `readText` | `(path) → string` |
| `readFile` | `(path) → { b64 } \| { __ow_shm }` |
| `writeFile` | `(path, data, encoding?) → null` |
| `readDir` | `(path) → { name, type }[]` |
| `stat` | `(path) → { size, isFile, isDir, mtimeMs } \| null` |
| `mkdir` | `(path, recursive?) → null` |
| `remove` | `(path, recursive?) → null` |
| `exists` | `(path) → boolean` |
| `copy` | `(src, dest, overwrite?) → null` |
| `rename` | `(oldPath, newPath) → null` |
| `chmod` | `(path, mode) → null` |
| `symlink` | `(target, linkPath) → null` |
| `readlink` | `(path) → string` |
| `lstat` | `(path) → { isSymlink, isFile, isDir, isCharDevice, isFifo, isSocket } \| null` |
| `realpath` | `(path) → string` |
| `mkdtemp` | `(prefix?) → string` |
| `access` | `(path, mode?) → boolean` |
| `truncate` | `(path, size) → null` |
| `utimes` | `(path, atimeMs, mtimeMs) → null` |
| `open` | `(path, flags?) → { fd }`* |
| `read` | `(fd, offset?, length?) → { b64 \| __ow_shm, eof }` |
| `write` | `(fd, data, offset?) → number` |
| `size` | `(fd) → number` |
| `close` | `(fd) → null` |
| `watch` | `(path, recursive?) → watcherId` |
| `unwatch` | `(watcherId) → null` |

\* `open` returns a numeric fd serialized as a JSON number (the SDK type is
`{ fd }` is illustrative; the raw result is the id). Treat the returned value as
the handle.

## Return shapes

```ts
// readFile, file >= 256 KB
{ __ow_shm: { id: string, size: number } }
// readFile, file < 256 KB
{ b64: string }

// stat
{ size: number, isFile: boolean, isDir: boolean, mtimeMs: number } | null

// lstat (does NOT follow symlinks)
{ isSymlink, isFile, isDir, isCharDevice, isFifo, isSocket } | null
```

## Details

- `writeFile` creates missing parent directories. `encoding` is `'utf8'`
  (default) or `'base64'`.
- `remove(path, true)` uses `remove_all`; `remove(path, false)` removes an empty
  directory or a file.
- `copy` is always recursive; `overwrite` controls `overwrite_existing`.
- `chmod` takes a numeric octal mode (e.g. `0o644`).
- `mkdtemp` uses a `mkdtemp(3)`-style template; **Windows v1 returns an error**.
- `utimes` is not implemented on **Windows v1** (returns an error).
- `access` default mode is read (`4`); on Windows it falls back to `exists`.
- `read` default length is 4 MB; chunks ≥ 256 KB return a `__ow_shm` handle.
- `open` flags: `r`, `w`, `a`, `r+`, `w+`.

## Events

`watch` emits `fs.watch` with batched changes (~50 ms):

```ts
{ watcherId: number, events: [{ type: string, path: string }] }
```

The watcher is bound to the window that registered it.

## Platform notes

- Watchers: inotify (Linux), `ReadDirectoryChangesW` (Windows).
- `chmod` follows POSIX semantics; on Windows only the read-only bit matters.
- The Windows implementation is present but marked `VERIFY IN CI`.

## See also

- [Filesystem guide](../../guides/filesystem.md)
- [`path`](path.md)
- [SDK path helpers via `app.getPath`](../../api/sdk/app.md)
