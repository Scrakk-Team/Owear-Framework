<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Auto-update de Owear

El auto-updater de Owear descarga **solo lo que cambia** (delta por bloques),
verifica **integridad** (sha256/sha512) y **autenticidad** (firma Ed25519), y
aplica el update con **reemplazo atómico + relaunch** del binario.

La lógica vive en el **SDK** (`@owear/core` → `autoUpdater`, proceso principal)
porque ahí hay HTTP, `crypto` y eventos; el **módulo nativo `updater`** hace solo
lo privilegiado: `state()` (versión/exe/dir/modo) y `apply({path})` (reemplazo
atómico + `execv` / `CreateProcessW`).

## Piezas

| Pieza | Quién | Qué hace |
|---|---|---|
| `manifest.yml` | `ow update` | versión, hashes, tamaño, `blockSize`, URL del blockmap, nota y **firma** |
| `*.blockmap` | `ow update` | `{ blockSize, size, blocks: [sha256…] }` — un hash por bloque |
| `autoUpdater` | SDK (`@owear/core`) | `check` → `download` (delta) → `quitAndInstall` + eventos |
| `updater` (nativo) | kernel | `state`, `apply` (atómico + relaunch) |

## Publicar un update

```bash
# 1) par de claves Ed25519 (una vez). La pública va en el bridge:
#    owear.bridge.ts → app.updater.publicKey  (o setFeedURL({ publicKey }))
ow update --gen-key owear-signing
#   → owear-signing.pem      (privada, chmod 600)
#   → owear-signing.pub.b64  (pública SPKI base64)

# 2) genera el artefacto y publica el canal
ow build app --format binary        # → release/MiApp-1.4.0
ow update \
  --file release/MiApp-1.4.0 \
  --version 1.4.0 \
  --channel latest \
  --url https://up.miapp.dev \
  --key owear-signing.pem \
  --notes @CHANGELOG.md \
  --out-dir release
#   → release/MiApp-1.4.0.blockmap
#   → release/latest.yml   (manifiesto YAML firmado)
```

Sube a tu hosting **los tres ficheros** con esa estructura:

```
https://up.miapp.dev/latest.yml
https://up.miapp.dev/MiApp-1.4.0
https://up.miapp.dev/MiApp-1.4.0.blockmap
```

## Manifiesto (`latest.yml`)

```yaml
version: 1.4.0
releaseDate: '2026-09-29T10:00:00.000Z'
path: MiApp-1.4.0
sha256: 36fd89a6…            # integridad del artefacto completo
sha512: f9902aa2…
size: 12345678
blockSize: 262144            # 256 KiB (tamaño de bloque del delta)
blockmap: https://up.miapp.dev/MiApp-1.4.0.blockmap
mandatory: false
signature: hMyb+IVSla…       # Ed25519 sobre "<version>:<sha256>" (base64)
notes: |
  Arreglos varios
```

La firma cubre `"<version>:<sha256>"` (no el YAML completo): evita problemas de
canonicalización y ata versión + contenido. Sin clave pública configurada la
firma no se exige, pero **el sha256 siempre se verifica**.

## Usar el auto-updater (proceso principal)

```ts
import { app, autoUpdater } from '@owear/core'

await app.whenReady()

autoUpdater.setFeedURL({
  provider: 'generic',                 // o 'github' (owner/repo)
  url: 'https://up.miapp.dev',
  channel: 'latest',
  publicKey: process.env.OW_PUBKEY,    // SPKI base64 o PEM (recomendado)
})

autoUpdater.on('checking-for-update', () => {})
autoUpdater.on('update-available', (info) => console.log('hay', info.version))
autoUpdater.on('update-not-available', () => {})
autoUpdater.on('download-progress', (p) => console.log(`${p.percent}%  ${p.bytesPerSecond} B/s`))
autoUpdater.on('update-downloaded', () => {/* avisar: reiniciar para aplicar */})
autoUpdater.on('error', (e) => console.error(e))

await autoUpdater.checkForUpdates()   // con autoDownload=true descarga ya
await autoUpdater.quitAndInstall()    // reemplaza el binario y relanza
```

### Modo automático / obligatorio

- `autoUpdater.autoDownload = true` (por defecto): `checkForUpdates()` descarga.
- `autoUpdater.channel = 'beta'`: usa `beta.yml` del feed.
- `info.mandatory` (manifiesto `mandatory: true`): ideal para bloquear el arranque
  hasta actualizar.

## Cómo funciona el delta

1. `checkForUpdates()` descarga el `latest.yml`, **verifica la firma** y compara
   versiones (semver).
2. `downloadUpdate()` lee el blockmap y calcula los `sha256` por bloque del
   binario **instalado** (`updater.state().exe`).
3. Los bloques cuyo hash coincide **se reutilizan**; solo los cambiados se piden
   con `Range: bytes=…` (agrupando bloques contiguos en un rango).
4. Se ensambla el artefacto nuevo, se verifica `sha256`/`sha512` y se guarda en un
   temporal.
5. `quitAndInstall()` llama a `updater.apply({ path })` → reemplazo atómico
   (`rename` en el mismo FS; si no, `copy` + `rename`) y **relanzamiento** del
   proceso (`execv` en Unix, `CreateProcessW` + `TerminateProcess` en Windows).

Si no hay blockmap (o falla), cae a **descarga completa** con progreso.

## Seguridad

- **Firma Ed25519** del manifiesto: aunque el feed sea HTTP, un atacante no puede
  publicar un update sin la clave privada (que nunca sale de tu CI/local).
- **Integridad** sha256 + sha512 del artefacto; cada bloque se valida al
  ensamblar (el ensamblado final se verifica entero).
- Guarda la clave privada fuera del repo (`owear-signing.pem`) y rota la pública
  en una release nueva si se compromete.
- En Windows, además, se recomienda **Authenticode** sobre el artefacto
  (pendiente de hook de firma).

## Tests

```bash
cd packages/core
npm run build
node --test test/updater.test.mjs        # YAML, semver, delta, Ed25519
node --test test/updater-feed.test.mjs   # feed real: tool + HTTP Range + delta
```

- `test/updater.test.mjs`: parser YAML, comparación semver, plan de delta
  (bloques cambiados / reutilizados) y firma Ed25519 (SPKI base64).
- `test/updater-feed.test.mjs`: genera artefacto + blockmap + manifiesto firmado
  con `ow update`, los sirve por HTTP con soporte `Range` y comprueba que se
  descargan **solo los bloques cambiados** y que el artefacto ensamblado coincide
  con el `sha256` del manifiesto.
- E2E: `tests/e2e/run_suites.py --sdk` incluye `sdk.updater.state` (módulo nativo
  vivo contra el kernel).
