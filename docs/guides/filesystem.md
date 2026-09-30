---
title: Filesystem (fs) and paths (path)
description: fs is a stock .owm module covering reads, writes, metadata, fd-style
order: 6
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Filesystem (`fs`) and paths (`path`)

`fs` is a stock `.owm` module covering reads, writes, metadata, fd-style
handles, and watchers. `path` provides pure path math plus OS standard
directories. Both are callable from the renderer and (via `invokeNative`) from
the main process.

```ts
import { invokeNative } from '@owear/core'
// renderer: ow.invoke('fs', …)
const text = await ow.invoke<string>('fs', 'readText', '/etc/hostname')
```

## Reading and writing

```ts
fs.readText(path): Promise<string>
fs.writeFile(path, data, encoding?: 'utf8' | 'base64'): Promise<null>
  // creates missing parent directories
fs.readDir(path): Promise<{ name: string; type: 'file' | 'dir' | 'other' }[]>
fs.exists(path): Promise<boolean>
fs.stat(path): Promise<{ size, isFile, isDir, mtimeMs } | null>
fs.mkdir(path, recursive?): Promise<null>
fs.remove(path, recursive?): Promise<null>   // recursive → remove_all
```

`stat` returns `null` for a missing path rather than throwing.

## Reading binaries (and large files)

```ts
fs.readFile(path): Promise<{ b64: string } | { __ow_shm: { id: string; size: number } }>
```

Files **smaller than 256 KB** come back as base64. Files **256 KB or larger**
are published to shared memory; read them with `ow.readShared`:

```ts
const res = await ow.invoke<any>('fs', 'readFile', '/tmp/video.mp4')
const bytes = res.__ow_shm ? await ow.readShared(res.__ow_shm) : bufFromBase64(res.b64)
```

This avoids both the base64 blow-up and an extra kernel copy for large payloads.

## Metadata and structure

```ts
fs.copy(src, dest, overwrite?): Promise<null>
fs.rename(oldPath, newPath): Promise<null>
fs.chmod(path, mode): Promise<null>            // numeric octal mode
fs.symlink(target, linkPath): Promise<null>
fs.readlink(path): Promise<string>
fs.lstat(path): Promise<{ isSymlink, isFile, isDir, isCharDevice, isFifo, isSocket } | null>
fs.realpath(path): Promise<string>
fs.mkdtemp(prefix?): Promise<string>           // not supported on Windows v1
fs.access(path, mode?): Promise<boolean>       // mode 4 = read (default)
fs.truncate(path, size): Promise<null>
fs.utimes(path, atimeMs, mtimeMs): Promise<null>  // not supported on Windows v1
```

`lstat` does **not** follow symlinks (it uses the platform's link status);
`stat` does.

## fd-style handles

For incremental reads/writes, open a handle and read in chunks. This is the tool
for very large files.

```ts
fs.open(path, flags?: 'r' | 'w' | 'a' | 'r+' | 'w+'): Promise<{ fd: number }>
fs.read(fd, offset?, length?): Promise<{ b64 | __ow_shm, eof: boolean }>
fs.write(fd, data: string | { b64 }, offset?): Promise<number>
fs.size(fd): Promise<number>
fs.close(fd): Promise<null>
```

Reads of 256 KB or more again return a `__ow_shm` handle. The default read
length is 4 MB.

```ts
const { fd } = await ow.invoke<{ fd: number }>('fs', 'open', '/tmp/big.bin', 'r')
try {
  let chunk
  do {
    chunk = await ow.invoke<any>('fs', 'read', fd, undefined, 1024 * 1024)
    const bytes = chunk.__ow_shm ? await ow.readShared(chunk.__ow_shm)
                                  : Buffer.from(chunk.b64, 'base64')
    consume(bytes)
  } while (!chunk.eof)
} finally {
  await ow.invoke('fs', 'close', fd)
}
```

## Watching directories

```ts
fs.watch(path, recursive?): Promise<number>   // returns a watcherId
fs.unwatch(watcherId): Promise<null>
```

Watch events are batched (~50 ms) and delivered as `fs.watch`:

```ts
const watcherId = await ow.invoke<number>('fs', 'watch', '/home/me/project', false)
ow.on('fs.watch', (payload) => {
  // { watcherId, events: [{ type, path }] }
  for (const e of payload.events) console.log(e.type, e.path)
})
```

The native backends are inotify (Linux) and `ReadDirectoryChangesW` (Windows).
The event `type` reflects the platform change kind.

## `path`

Pure, cross-platform path helpers plus standard directories. Functions are
mirrored from the native `path` module.

```ts
path.join('a', 'b', 'c')        // "a/b/c"
path.resolve('/base', 'x', '..', 'y')  // absolute, normalized
path.dirname('/a/b/c.txt')      // "/a/b"
path.basename('/a/b/c.txt', '.txt')    // "c"
path.extname('archive.tar.gz')  // ".gz"
path.normalize('/a/./b/../c')   // "/a/c"
```

Standard directories (returned as absolute paths):

```ts
path.homeDir() / appDataDir() / userDataDir() / cacheDir() / tempDir()
path.configDir() / exeDir() / cwd()
```

The same directories are also exposed from the SDK as `app.getPath(name)` (see
[Main process](main-process.md)); use whichever is closer to your code.

## Platform notes

- `mkdtemp` and `utimes` are not implemented on Windows v1 and return a clear
  error.
- `chmod` follows Unix semantics; on Windows only the read-only bit is
  meaningful.
- Watchers require a real directory; watching a file path returns an error.

## Next steps

- [`fs` module reference](../api/modules/fs.md) and [`path`](../api/modules/path.md).
- [Renderer API](renderer-api.md) — large payloads and shared memory.
