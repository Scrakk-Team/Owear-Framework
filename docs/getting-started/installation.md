---
title: Installation
description: Owear ships as a CLI (@owear/cli) plus a per-platform runtime package that
order: 2
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Installation

Owear ships as a CLI (`@owear/cli`) plus a per-platform runtime package that
contains the native kernel and the stock modules. This page covers both the
published flow and building from a checkout while packages are not yet on npm.

## Requirements

- **Node.js 20+** and **pnpm** (or npm) for the JavaScript side.
- **CMake 3.20+** and a C++20 compiler only if you build the kernel or write
  native modules.
- Platform libraries (only needed to build the kernel from source):

**Linux**

```bash
sudo apt-get install -y libgtk-3-dev libwebkit2gtk-4.1-dev libsoup-3.0-dev \
  libssl-dev zlib1g-dev ninja-build xvfb libayatana-appindicator3-dev
```

**Windows** — MSVC (Visual Studio 2022) and vcpkg for `zlib`/`openssl`. WebView2
support is gated behind `OW_WITH_WEBVIEW2` and needs the
`Microsoft.Web.WebView2` NuGet package under `deps/webview2/`.

## Option A — from npm (recommended)

The `@owear/*` packages are published on npm (**0.1.4**): `@owear/cli`,
`@owear/core`, `@owear/vite-plugin` and the per-platform runtime
(`@owear/linux-x64-gnu`, `@owear/win32-x64`).

```bash
pnpm dlx @owear/cli create my-app    # or: npm i -g @owear/cli && ow create my-app
cd my-app
pnpm install
pnpm dev
```

The CLI resolves the kernel from the runtime package for your platform:

| Platform | Package |
|---|---|
| Linux x64 | `@owear/linux-x64-gnu` |
| Windows x64 | `@owear/win32-x64` |

## Option B — from a checkout (framework contributors)

```bash
git clone <owear-repo> owear
cd owear

# 1) build the kernel for your platform
pnpm install
pnpm build:native          # or: cmake --preset linux-release && cmake --build --preset linux-release

# 2) scaffold an app anywhere
node packages/cli/src/ow.js create ../my-app
cd ../my-app
pnpm install
pnpm dev
```

`ow dev` looks for the kernel in this order:

1. `OW_KERNEL_BIN` if set;
2. `<app>/.owear/bin/owear`;
3. the local monorepo build (`build/<preset>/src/owear`);
4. the installed runtime package.

If it cannot find a kernel and source is available, `ow dev` compiles it the
first time. Otherwise it stops with instructions.

## Verifying the install

```bash
ow --help            # prints commands
ow api list          # shows framework APIs (only inside the Owear repo)
```

Inside a project:

```bash
pnpm dev             # opens a window with your app
```

## Next steps

- [Quick start](quickstart.md) — create and run your first app.
- [Project structure](project-structure.md) — what the generated files do.
