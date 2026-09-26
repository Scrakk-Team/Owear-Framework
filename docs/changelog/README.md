<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Changelog

Un fichero por release: `changelog-<versión>.md` (por ejemplo `changelog-0.1.1.md`),
siguiendo el formato de [Keep a Changelog](https://keepachangelog.com/).

## Cómo se hace un release

1. **Sube la versión** (fuente de verdad: `package.json` de la raíz):
   ```bash
   node tools/version.mjs set 0.2.0
   ```
2. **Escribe** `docs/changelog/changelog-0.2.0.md` con lo añadido/cambiado/corregido.
3. **Verifica**:
   ```bash
   node tools/version.mjs check
   node tools/gen-apis.mjs --check
   node tools/check-apis.mjs
   node tools/license-header.mjs --check
   ```
4. **Commit y tag**:
   ```bash
   git commit -am "chore(release): 0.2.0"
   git tag v0.2.0
   git push origin master --tags
   ```
5. El workflow `release.yml` compila, publica en npm y crea el **GitHub Release**
   usando este fichero como notas.

## Secciones

`### Added` · `### Changed` · `### Fixed` · `### Removed` · `### Security`

## npm y autenticación

Hoy `release.yml` publica con el secreto **`NPM_TOKEN`** (granular con bypass 2FA)
y firma con **provenance**.

**Migración a Trusted Publishing (OIDC, sin token)** — recomendado, porque los
tokens bypass-2FA pierden el publish directo ~enero 2027:

1. En npmjs.com → cada paquete → **Settings → Trusted Publisher**:
   - Provider: **GitHub Actions**
   - Repository: `Scrakk/Owear-Framework`
   - Workflow: `release.yml`
   - Environment: (vacío)
2. Hecho eso, en `release.yml` **quita** el `env: NODE_AUTH_TOKEN` del paso de
   publish (el `id-token: write` ya está) y borra el secreto `NPM_TOKEN`.

