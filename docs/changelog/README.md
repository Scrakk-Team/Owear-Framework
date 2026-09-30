---
title: Changelog
description: One file per release — changelog-<version>.md (for example changelog-0.1.1.md).
order: 1
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Changelog

One file per release: `changelog-<version>.md` (for example `changelog-0.1.1.md`),
following the [Keep a Changelog](https://keepachangelog.com/) format.

## How a release is cut

1. **Bump the version** (source of truth: the root `package.json`):
   ```bash
   node tools/version.mjs set 0.2.0
   ```
2. **Write** `docs/changelog/changelog-0.2.0.md` with what was added/changed/fixed.
3. **Verify**:
   ```bash
   node tools/version.mjs check
   node tools/gen-apis.mjs --check
   node tools/check-apis.mjs
   node tools/license-header.mjs --check
   ```
4. **Commit and tag**:
   ```bash
   git commit -am "chore(release): 0.2.0"
   git tag v0.2.0
   git push origin master --tags
   ```
5. The `release.yml` workflow builds, publishes to npm and creates the **GitHub
   Release**, using this file as the notes.

## Sections

`### Added` · `### Changed` · `### Fixed` · `### Removed` · `### Security`

## npm and authentication

Today `release.yml` publishes with the **`NPM_TOKEN`** secret (granular with 2FA
bypass) and signs with **provenance**.

**Migrating to Trusted Publishing (OIDC, no token)** — recommended, because
bypass-2FA tokens lose direct publish around January 2027:

1. On npmjs.com → each package → **Settings → Trusted Publisher**:
   - Provider: **GitHub Actions**
   - Repository: `Scrakk/Owear-Framework`
   - Workflow: `release.yml`
   - Environment: (empty)
2. Once done, in `release.yml` **remove** the `env: NODE_AUTH_TOKEN` from the
   publish step (the `id-token: write` is already there) and delete the
   `NPM_TOKEN` secret.
