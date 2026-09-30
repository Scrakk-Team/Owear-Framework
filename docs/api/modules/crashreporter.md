---
title: crashreporter
description: Captures native crashes with a signal + backtrace log.
order: 5
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# `crashreporter`

Captures native crashes with a signal + backtrace log.

- **Kind:** builtin
- **Version:** 0.1.0
- **Platforms:** Linux (signals + glibc backtrace), Windows

## Functions

| Function | Signature |
|---|---|
| `install` | `() → crashDir` |
| `lastCrashLog` | `() → string \| null` |

## Behavior

`install()` registers handlers for `SIGSEGV`, `SIGABRT`, `SIGFPE`, `SIGBUS`, and
`SIGILL`. When a signal fires, it writes `crash-<pid>.log` (signal, `strsignal`,
pid, timestamp, and up to 32 `backtrace()` frames) into the crash directory, then
restores the default handler and re-raises the signal so a core dump is still
produced.

The crash directory is `$XDG_CACHE_HOME/owear/crashes` (falling back to
`$HOME/.cache/owear/crashes`). Files are created with mode `0600`.

```ts
const dir = await ow.invoke<string>('crashreporter', 'install')
const last = await ow.invoke<string | null>('crashreporter', 'lastCrashLog')
```

## Uploading crashes

The module writes files and returns paths; uploading them to your own telemetry
is up to your app.

## Notes

- `backtrace()` is not strictly async-signal-safe, but it is the best available
  without extra dependencies.
- Windows uses its own crash mechanism (verify in CI).

## See also

- [Debugging guide](../../guides/debugging.md)
