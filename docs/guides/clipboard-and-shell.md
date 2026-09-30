---
title: Clipboard and shell
description: Two small modules for OS integration  clipboard (text and images) and shell
order: 2
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Clipboard and shell

Two small modules for OS integration: `clipboard` (text and images) and `shell`
(open URLs, files, and folders).

## Clipboard

```ts
// text
const text = await ow.invoke<string>('clipboard', 'readText')
await ow.invoke('clipboard', 'writeText', 'hello')

// image (PNG)
const img = await ow.invoke<any>('clipboard', 'readImage')
// img is null when the clipboard holds no image, otherwise:
// { __ow_shm: { id, size }, width, height, format: 'png' }
if (img) {
  const pngBytes = await ow.readShared(img.__ow_shm)
}

await ow.invoke('clipboard', 'writeImage', pngBase64)
await ow.invoke('clipboard', 'clear')
```

Functions:

```ts
clipboard.readText(): Promise<string>
clipboard.writeText(text): Promise<null>
clipboard.readImage(): Promise<{ __ow_shm, width?, height?, format } | null>
clipboard.writeImage(pngBase64): Promise<null>
clipboard.clear(): Promise<null>
```

Images always travel as PNG. Reading returns a shared-memory handle, so the
larger the clipboard image, the more you benefit from `ow.readShared`. Windows
image support (WIC) is pending; text works everywhere.

## Shell

```ts
await ow.invoke('shell', 'openExternal', 'https://owear.dev')
await ow.invoke('shell', 'openPath', '/home/me/report.pdf')
await ow.invoke('shell', 'showItemInFolder', '/home/me/report.pdf')
```

Functions:

```ts
shell.openExternal(url): Promise<null>       // only http(s) and mailto are allowed
shell.openPath(path): Promise<null>          // open with the default application
shell.showItemInFolder(path): Promise<null>  // reveal in the file manager
```

`openExternal` validates the scheme and refuses anything other than `http`,
`https`, and `mailto` — a deliberate safety measure to avoid launching arbitrary
schemes from untrusted content.

### Platform notes

- Linux: `GAppInfo` for URLs/paths and the `org.freedesktop.FileManager1` D-Bus
  interface for `showItemInFolder` (with a fallback that opens the parent
  directory).
- Windows: `ShellExecute` / `IShellLink` (verify in CI).

## Next steps

- [`clipboard` module reference](../api/modules/clipboard.md) and
  [`shell`](../api/modules/shell.md).
