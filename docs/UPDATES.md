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
signature: hMyb+IVSla…       # Ed25519 sobre "<version>:<sha256>" (metadata)
binarySig: Vf3k…             # Ed25519 sobre los bytes del artefacto (payload)
notes: |
  Arreglos varios
```

La firma cubre `"<version>:<sha256>"` (no el YAML completo): evita problemas de
canonicalización y ata versión + contenido. `binarySig` firma el **artefacto
completo** (defensa en profundidad: permite verificar el binario de forma
independiente del feed). Sin clave pública configurada la firma no se exige,
pero **el sha256 siempre se verifica**.

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

## Firma del binario

Dos mecanismos (`packages/cli/tools/owear-sign.mjs`, cableado en
`ow build app|installer|uninstaller`):

```bash
# Ed25519 desprendida → <artifact>.sig  (cualquier plataforma, sin deps)
ow build app --format binary --sign-key owear-signing.pem

# Authenticode (Windows PE/MSI) con un PFX; requiere osslsigncode o signtool
ow build installer --pfx cert.p12 --pfx-password-env OW_SIGN_PFX_PASSWORD \
  --timestamp http://timestamp.digicert.com

# o por entorno
export OW_SIGN_KEY=owear-signing.pem
export OW_SIGN_PFX=cert.p12 OW_SIGN_PFX_PASSWORD=…
export OW_SIGN_TIMESTAMP=http://timestamp.digicert.com
ow build app
```

- `--require-sign` convierte "no se pudo firmar" en error de build.
- Sin material de firma el build **no se rompe** (solo avisa).
- Authenticode ancla la confianza de **editor** en el SO (SmartScreen,
  "Unknown publisher"); Ed25519 da verificación criptográfica portable.
- La firma Ed25519 del artefacto es la misma clave que firma el manifiesto;
  `ow update --key <pem>` emite `signature` (metadata) y `binarySig` (payload).

## Reintentos y reanudación

`autoUpdater` reintenta con **backoff exponencial** (con jitter) ante errores de
red y estados transitorios (`408/425/429/5xx`), y aplica **timeout por petición**:

```ts
autoUpdater.maxRetries = 3        // reintentos por petición
autoUpdater.retryDelay = 800      // ms base (×2 cada intento, tope 15 s)
autoUpdater.requestTimeout = 60_000
```

Si una **descarga completa** se corta a mitad, se reanuda con
`Range: bytes=<recibido>-` (no reinicia); si el servidor ignora el `Range`,
reinicia limpiamente. En el delta, cada rango se reintenta igual. Si el blockmap
o el `Range` fallan, cae a descarga completa.

## Rollback

En `apply()` se guarda el binario actual en `<exe>.owprev` (por defecto) y solo
se borra al **confirmar** el update. Si el binario nuevo entra en crash loop, se
revierte:

```ts
// al arrancar (tras un update), arma el guard
app.whenReady().then(() => autoUpdater.armBootGuard({ threshold: 3, healthDelayMs: 10_000 }))
// → 'idle' | 'armed' | 'rolled-back'
```

- `armBootGuard()` solo actúa si hay un update pendiente (`state().hasRollback`).
- Cuenta arranques sin confirmar en `<userData>/update-boot.json`; al llegar al
  umbral llama a `rollback()` (restaura `<exe>.owprev` y relanza).
- Si el arranque se mantiene sano `healthDelayMs`, confirma (`commit()`) y borra
  el backup.
- Manual: `autoUpdater.rollback()` y `autoUpdater.commitUpdate()`.

Aplicación: `autoUpdater.quitAndInstall({ backup: true })` (por defecto) o
`{ backup: false }` para no dejar copia.

## Seguridad

- **Firma Ed25519** del manifiesto: aunque el feed sea HTTP, un atacante no puede
  publicar un update sin la clave privada (que nunca sale de tu CI/local).
- **binarySig** (Ed25519 del artefacto) e **integridad** sha256 + sha512; cada
  bloque se valida al ensamblar (y el ensamblado final entero).
- **Authenticode** (opcional) ancla la confianza de editor en Windows.
- Guarda la clave privada fuera del repo (`owear-signing.pem`) y rota la pública
  en una release nueva si se compromete.

## Tests

```bash
cd packages/core
npm run build
npm test   # incluye updater.test / updater-feed.test / updater-boot.test /
           # updater-sign.test / updater-retry.test
```

- `test/updater.test.mjs`: parser YAML, comparación semver, plan de delta
  (bloques cambiados / reutilizados) y firma Ed25519 (SPKI base64).
- `test/updater-feed.test.mjs`: genera artefacto + blockmap + manifiesto firmado
  con `ow update`, los sirve por HTTP con soporte `Range` y comprueba que se
  descargan **solo los bloques cambiados**, el `binarySig` del payload y que el
  artefacto ensamblado coincide con el `sha256` del manifiesto.
- `test/updater-boot.test.mjs`: contador de arranques, detección de crash loop
  (rollback) y confirmación de salud.
- `test/updater-sign.test.mjs`: comando Authenticode (osslsigncode/signtool),
  detección PE/MSI y firma/verificación Ed25519 desprendida.
- `test/updater-retry.test.mjs`: reintentos (500 transitorios), reanudación de
  descarga con `Range` y timeout por petición.
- E2E: `tests/e2e/run_suites.py --sdk` incluye `sdk.updater.state` (módulo nativo
  vivo contra el kernel); `tests/e2e/update_apply.py` prueba **apply + backup +
  rollback + commit** sobre una copia del kernel (reemplazo atómico y relaunch
  reales).
