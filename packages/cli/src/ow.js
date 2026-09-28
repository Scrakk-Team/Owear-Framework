#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// ow — CLI del framework Owear.
//   ow create <dir>   scaffoldea una app
//   ow dev            kernel nativo + vite dev server + sidecar Node
//   ow build          build de producción (vite build + módulos .owm)
//
import { spawn, spawnSync } from 'node:child_process'
import * as fs from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)
const __dirname = path.dirname(fileURLToPath(import.meta.url))

const C = {
  reset: '\x1b[0m',
  dim: '\x1b[2m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  red: '\x1b[31m',
  cyan: '\x1b[36m',
}
const log = (msg) => console.log(`${C.dim}[ow]${C.reset} ${msg}`)
const die = (msg) => {
  console.error(`${C.red}[ow] ✗${C.reset} ${msg}`)
  process.exit(1)
}

function platformPreset() {
  switch (process.platform) {
    case 'linux': return 'linux-release'
    case 'win32': return 'windows-release'
    case 'darwin': return 'macos-release'
    default: die(`plataforma no soportada: ${process.platform}`)
  }
}

/** Nombre del paquete de runtime para la plataforma actual (null si no hay). */
function runtimePackageName() {
  if (process.platform === 'linux' && process.arch === 'x64') return '@owear/linux-x64-gnu'
  if (process.platform === 'win32' && process.arch === 'x64') return '@owear/win32-x64'
  if (process.platform === 'darwin' && process.arch === 'arm64') return '@owear/darwin-arm64'
  if (process.platform === 'darwin' && process.arch === 'x64') return '@owear/darwin-x64'
  return null
}

/** Directorio del paquete de runtime instalado (o null). */
function runtimePackageDir() {
  const name = runtimePackageName()
  if (!name) return null
  try {
    return path.dirname(require.resolve(name + '/package.json'))
  } catch {
    return null
  }
}

/**
 * Ruta(s) de los módulos stock, separadas por `path.delimiter`.
 * - runtime npm:  <pkg>/bin/modules (plano)
 * - monorepo:     cada build/<preset>/api/<nombre>/ (el loader NO recursiona)
 */
function stockModulesPath() {
  // Dev: si el usuario fija el kernel (OW_KERNEL_BIN), sus módulos viven junto
  // a él (<exe>/modules). Es la fuente correcta: kernel y módulos del mismo build.
  const kb = process.env.OW_KERNEL_BIN
  if (kb) {
    const m = path.join(path.dirname(kb), 'modules')
    if (fs.existsSync(m)) return m
  }
  // Dev en el monorepo: preferir el BUILD LOCAL (mismo build que el kernel)
  // ANTES del runtime package, que puede ser un artefacto prebuilt antiguo.
  const api = path.resolve(__dirname, '../../../build', platformPreset(), 'api')
  if (fs.existsSync(api)) {
    const dirs = fs
      .readdirSync(api, { withFileTypes: true })
      .filter((d) => d.isDirectory() && d.name !== 'CMakeFiles')
      .map((d) => path.join(api, d.name))
    if (dirs.length) return dirs.join(path.delimiter)
  }
  const rt = runtimePackageDir()
  if (rt) {
    const m = path.join(rt, 'bin', 'modules')
    if (fs.existsSync(m)) return m
  }
  return ''
}

/** Localiza el binario del kernel. Orden: OW_KERNEL_BIN → local → runtime npm → monorepo. */
function findKernelBin(cwd = process.cwd()) {
  if (process.env.OW_KERNEL_BIN && fs.existsSync(process.env.OW_KERNEL_BIN)) {
    return process.env.OW_KERNEL_BIN
  }
  const exe = process.platform === 'win32' ? 'owear.exe' : 'owear'
  const rt = runtimePackageDir()
  const candidates = [
    path.join(cwd, '.owear', 'bin', exe),
    // monorepo en desarrollo: el kernel recién compilado manda sobre el paquete npm
    path.resolve(__dirname, '../../../build', platformPreset(), 'src', exe),
    ...(rt ? [path.join(rt, 'bin', exe)] : []),
  ]
  for (const c of candidates) if (fs.existsSync(c)) return c
  return null
}

/** Compila el kernel si hay fuentes disponibles (repo hermano o checkout). */
function ensureKernelBuilt(cwd) {
  const existing = findKernelBin(cwd)
  if (existing) return existing

  const repoRoot = path.resolve(__dirname, '../../..')
  const hasSources = fs.existsSync(path.join(repoRoot, 'CMakeLists.txt'))
  if (!hasSources) {
    // El binario del kernel todavía no se distribuye como paquete npm, así que
    // no prometemos `@owear/runtime-<platform>`: damos las salidas reales.
    const exe = process.platform === 'win32' ? 'owear.exe' : 'owear'
    die(
      `no encuentro el kernel owear. Opciones:
  · compílalo desde un checkout: cmake --preset ${platformPreset()} && cmake --build --preset ${platformPreset()}
  · copia el binario en ${path.join(process.cwd(), '.owear', 'bin', exe)}
  · o define OW_KERNEL_BIN=/ruta/al/owear
  (el binario precompilado por plataforma aún no se publica en npm)`)
  }
  log('compilando kernel nativo (primera vez)…')
  const preset = platformPreset()
  for (const args of [['--preset', preset], ['--build', '--preset', preset]]) {
    const r = spawnSync('cmake', args, { cwd: repoRoot, stdio: 'inherit' })
    if (r.status !== 0) die('fallo al compilar el kernel')
  }
  return findKernelBin(cwd)
}

// ── comandos ────────────────────────────────────────────────────────────────

async function main() {
  const [, , cmd, ...rest] = process.argv

  if (!cmd || cmd === '-h' || cmd === '--help') {
    printHelp(); return
  }

  switch (cmd) {
    case 'create': return cmdCreate(rest)
    case 'dev':    return cmdDev(rest)
    case 'build':  return cmdBuild(rest)
    case 'api':    return cmdApi(rest)
    default:
      die(`comando desconocido: ${cmd} (usa --help)`)
  }
}

function printHelp() {
  console.log(`
${C.cyan}owear${C.reset} — framework desktop nativo

  ${C.green}ow create <dir>${C.reset}   crea una app nueva
  ${C.green}ow dev${C.reset}            desarrollo: vite + kernel + sidecar node
  ${C.green}ow build${C.reset}          build de producción
  ${C.green}ow api list${C.reset}       lista las APIs del repo y sus manifiestos
  ${C.green}ow api new <nombre>${C.reset}  scaffoldea una API (api/<nombre>/ + manifiesto)

Variables útiles:
  OW_KERNEL_BIN     ruta al binario owear
`)
}

function cmdCreate(args) {
  const dir = args[0]
  if (!dir) die('uso: ow create <dir>')
  const target = path.resolve(dir)
  if (fs.existsSync(target) && fs.readdirSync(target).length) {
    die(`el directorio ya existe y no está vacío: ${target}`)
  }
  const templateDir = path.resolve(__dirname, '../template')
  if (!fs.existsSync(templateDir)) {
    die(`template no encontrado en ${templateDir} (instala @owear/cli completo)`)
  }
  fs.cpSync(templateDir, target, { recursive: true })
  applyAppName(target)
  log(`app creada en ${target}`)
  log('siguientes pasos:')
  console.log(`  cd ${path.basename(target)}`)
  console.log('  npm install')
  console.log('  npm run dev')
}

/**
 * Sustituye los placeholders del template por el nombre de la app
 * (__APP_NAME__ legible, __PKG_NAME__ válido como nombre de paquete npm).
 */
function applyAppName(target) {
  const appName = path.basename(target)
  const pkgName =
    appName
      .toLowerCase()
      .replace(/[^a-z0-9._-]+/g, '-')
      .replace(/^[-._]+|[-._]+$/g, '') || 'owear-app'
  const extRe = /\.(ts|tsx|js|mjs|json|html|css|md|txt|ya?ml)$/
  const walk = (dir) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      if (entry.name === 'node_modules') continue
      const p = path.join(dir, entry.name)
      if (entry.isDirectory()) walk(p)
      else if (extRe.test(entry.name)) {
        const src = fs.readFileSync(p, 'utf8')
        const out = src
          .replaceAll('__APP_NAME__', appName)
          .replaceAll('__PKG_NAME__', pkgName)
        if (out !== src) fs.writeFileSync(p, out)
      }
    }
  }
  walk(target)
}

// ── ow api — gestión de las APIs del framework ──────────────────────────────
// Fuente única de verdad: api/<nombre>/owear.module.json. `ow api new` crea la
// carpeta + manifiesto + esqueleto C++ y regenera el descubrimiento.

/** Raíz del repo de Owear (para `ow api`, que no vive en la app). */
function owearRepoRoot() {
  const root = path.resolve(__dirname, '../../..')
  return fs.existsSync(path.join(root, 'CMakeLists.txt')) ? root : null
}

function cmdApi(args) {
  const root = owearRepoRoot()
  if (!root) die('`ow api` solo funciona dentro del repo de Owear (no encuentro api/ desde el CLI)')
  const sub = args[0]
  if (sub === 'list') return cmdApiList(root)
  if (sub === 'new') return cmdApiNew(root, args[1])
  die('uso: ow api <list | new <nombre>>')
}

function readApiManifests(root) {
  const apiDir = path.join(root, 'api')
  const out = []
  for (const d of fs.readdirSync(apiDir, { withFileTypes: true })) {
    if (!d.isDirectory()) continue
    const file = path.join(apiDir, d.name, 'owear.module.json')
    if (!fs.existsSync(file)) continue
    out.push(JSON.parse(fs.readFileSync(file, 'utf8')))
  }
  return out
}

function cmdApiList(root) {
  const manifests = readApiManifests(root).sort((a, b) => a.name.localeCompare(b.name))
  const fnCount = (m) =>
    m.kind === 'module'
      ? (m.functions?.length ?? 0)
      : (m.descriptors ?? []).reduce((a, d) => a + (d.functions?.length ?? 0), 0)
  const w = Math.max(4, ...manifests.map((m) => m.name.length))
  console.log(`\n${'API'.padEnd(w)}  KIND     VER    FNS  PLATAFORMAS`)
  console.log(`${'─'.repeat(w)}  ───────  ─────  ───  ────────────`)
  for (const m of manifests) {
    const fns = String(fnCount(m)).padStart(3)
    const plats = (m.platforms ?? [...new Set((m.descriptors ?? []).flatMap((d) => d.platforms ?? []))]).join('/') || '—'
    console.log(`${m.name.padEnd(w)}  ${m.kind.padEnd(7)}  ${m.version.padEnd(5)}  ${fns}  ${plats}`)
  }
  const mods = manifests.filter((m) => m.kind === 'module').length
  console.log(`\n${manifests.length} APIs — ${mods} módulos (.owm), ${manifests.length - mods} builtins\n`)
}

function cmdApiNew(root, name) {
  if (!name) die('uso: ow api new <nombre>')
  if (!/^[a-z][a-z0-9-]*$/.test(name)) die('nombre inválido (minúsculas, dígitos y guiones; debe empezar por letra)')
  const dir = path.join(root, 'api', name)
  if (fs.existsSync(dir) && fs.readdirSync(dir).length) die(`api/${name}/ ya existe y no está vacío`)

  fs.mkdirSync(path.join(dir, 'src'), { recursive: true })

  const manifest = {
    $schema: '../owear.module.schema.json',
    name,
    kind: 'module',
    version: '0.1.0',
    description: `${name} — API de Owear.`,
    platforms: ['linux', 'win', 'mac'],
    functions: ['ping'],
  }
  fs.writeFileSync(path.join(dir, 'owear.module.json'), JSON.stringify(manifest, null, 2) + '\n')

  fs.writeFileSync(
    path.join(dir, 'CMakeLists.txt'),
    `# ── API ${name} ────────────────────────────────────────────────────────────────\n` +
      `# Manifiesto: owear.module.json (fuente única de verdad).\n` +
      `ow_add_module(${name} SOURCES src/${name}.cpp)\n`
  )

  fs.writeFileSync(
    path.join(dir, 'src', `${name}.cpp`),
    `// Copyright 2026 Owear Contributors\n` +
      `// SPDX-License-Identifier: Apache-2.0\n` +
      `//\n` +
      `// api/${name}/src/${name}.cpp — implementación de la API ${name}.\n` +
      `// Se compila a ${name}.owm y se invoca desde el renderer con:\n` +
      `//   await ow.invoke('${name}', 'ping')\n` +
      `\n` +
      `#include <ow/Json.h>\n` +
      `#include <ow/Module.h>\n` +
      `#include "ow_api.h"\n` +
      `\n` +
      `static void ping(const ow_request_t*, ow_response_t* res) {\n` +
      `    ow::Module::RespondOk(res, "\\"pong\\"");\n` +
      `}\n` +
      `\n` +
      `OW_MODULE_BEGIN(${name}, "1.0.0")\n` +
      `OW_FN(ping)\n` +
      `OW_MODULE_END()\n`
  )

  fs.writeFileSync(
    path.join(dir, 'README.md'),
    `# ${name}\n\n` +
      `API de Owear. Manifiesto: [\`owear.module.json\`](./owear.module.json).\n\n` +
      `Desde el renderer:\n\n\`\`\`ts\nawait ow.invoke('${name}', 'ping') // → "pong"\n\`\`\`\n`
  )

  log(`API creada en api/${name}/`)
  log('regenerando descubrimiento (api/generated.cmake + builtins)…')
  const r = spawnSync(process.execPath, [path.join(root, 'tools', 'gen-apis.mjs')], { stdio: 'inherit' })
  if (r.status !== 0) die('falló tools/gen-apis.mjs')
  log(`listo. En el renderer: ow.invoke('${name}', 'ping')`)
}

function runProc(cmd, args, opts = {}) {
  return new Promise((resolve) => {
    const p = spawn(cmd, args, { stdio: 'inherit', ...opts })
    p.on('exit', (code) => resolve(code ?? 1))
  })
}

async function waitForServer(url, timeoutMs = 30000) {
  const start = Date.now()
  while (Date.now() - start < timeoutMs) {
    try {
      const r = await fetch(url)
      if (r.ok) return true
    } catch { /* aún no */ }
    await new Promise((r) => setTimeout(r, 250))
  }
  return false
}

/** Entry del proceso principal: app/main.{ts,mts,js,mjs}. */
function findMainEntry(cwd) {
  for (const name of ['main.ts', 'main.mts', 'main.js', 'main.mjs']) {
    const p = path.join(cwd, 'app', name)
    if (fs.existsSync(p)) return p
  }
  return null
}

/**
 * Prepara el entry del sidecar para que lo ejecute CUALQUIER node instalado.
 * Node no ejecuta TypeScript hasta la 22.6 (y hasta la 23.6 sólo con un flag),
 * así que compilamos con esbuild (viene con vite). De este modo el runtime del
 * sistema sirve y el arranque no depende de la versión que tenga el usuario.
 */
function prepareMain(cwd, outDir) {
  const entry = findMainEntry(cwd)
  if (!entry) return null
  if (entry.endsWith('.js') || entry.endsWith('.mjs')) return entry

  const out = path.join(outDir, 'main.js')
  fs.mkdirSync(outDir, { recursive: true })
  log(`compilando ${path.relative(cwd, entry)}…`)
  const r = spawnSync(
    'npx',
    [
      'esbuild', entry,
      '--bundle', '--platform=node', '--format=esm',
      '--packages=external', `--outfile=${out}`, '--log-level=warning',
    ],
    { cwd, stdio: 'inherit', shell: process.platform === 'win32' }
  )
  if (r.status !== 0) {
    die(`no se pudo compilar ${path.relative(cwd, entry)} — esbuild viene con vite, revisa que esté instalado`)
  }
  return out
}

/**
 * Entries de worker de la app: `app/workers/**\/*.{ts,mts,js,mjs}`.
 * Se compilan a <outDir>/workers/ conservando la ruta relativa, y el kernel
 * expone el directorio al main vía OW_APP_WORKERS para `app.forkWorker()`.
 */
function findWorkerEntries(cwd) {
  const dir = path.join(cwd, 'app', 'workers')
  if (!fs.existsSync(dir)) return []
  const out = []
  const walk = (d) => {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const p = path.join(d, e.name)
      if (e.isDirectory()) walk(p)
      else if (/\.(ts|mts|js|mjs)$/.test(e.name)) out.push(p)
    }
  }
  walk(dir)
  return out
}

function prepareWorkers(cwd, outDir) {
  const entries = findWorkerEntries(cwd)
  if (!entries.length) return null

  const root = path.join(cwd, 'app', 'workers')
  const workersDir = path.join(outDir, 'workers')
  const built = []

  for (const entry of entries) {
    const rel = path.relative(root, entry)
    const out = path.join(workersDir, rel.replace(/\.(ts|mts)$/, '.js'))
    fs.mkdirSync(path.dirname(out), { recursive: true })

    if (entry.endsWith('.js') || entry.endsWith('.mjs')) {
      fs.copyFileSync(entry, out)
    } else {
      log(`compilando worker ${path.relative(cwd, entry)}…`)
      const r = spawnSync(
        'npx',
        [
          'esbuild', entry,
          '--bundle', '--platform=node', '--format=esm',
          '--packages=external', `--outfile=${out}`, '--log-level=warning',
        ],
        { cwd, stdio: 'inherit', shell: process.platform === 'win32' }
      )
      if (r.status !== 0) die(`no se pudo compilar el worker ${path.relative(cwd, entry)}`)
    }
    built.push(out)
  }
  return workersDir
}

async function cmdDev() {
  const cwd = process.cwd()
  if (!fs.existsSync(path.join(cwd, 'package.json'))) die('ejecuta dentro de tu app')

  const kernelBin = ensureKernelBuilt(cwd)

  log('arrancando vite…')
  const vite = spawn('npx', ['vite', '--port', '5173', '--strictPort'], {
    cwd,
    stdio: 'inherit',
    shell: process.platform === 'win32',
  })

  const up = await waitForServer('http://localhost:5173/')
  if (!up) { vite.kill(); die('vite no arrancó') }
  log('dev server listo en http://localhost:5173')

  // compila módulos nativos de la app (native/*.cpp → .owm)
  const nativeDir = path.join(cwd, 'native')
  let modulesDir = ''
  if (fs.existsSync(nativeDir)) {
    modulesDir = path.join(cwd, '.owear', 'modules')
    fs.mkdirSync(modulesDir, { recursive: true })
    const r = spawnSync('npx', ['owear-build-native'], {
      cwd,
      stdio: 'inherit',
      shell: process.platform === 'win32',
      env: { ...process.env, OW_MODULES_OUT: modulesDir },
    })
    if (r.status !== 0) die('falló la compilación de native/*.cpp')
  }

  const mainJs = prepareMain(cwd, path.join(cwd, '.owear'))
  if (mainJs) log(`proceso principal: ${path.relative(cwd, mainJs)}`)

  const workersDir = prepareWorkers(cwd, path.join(cwd, '.owear'))
  if (workersDir) log(`workers: ${path.relative(cwd, workersDir)}`)

  log(`lanzando kernel: ${kernelBin}`)
  const kernel = spawn(kernelBin, [], {
    stdio: 'inherit',
    env: {
      ...process.env,
      OW_APP_NAME: JSON.parse(fs.readFileSync(path.join(cwd, 'package.json'), 'utf8')).name ?? 'Owear App',
      OW_DEV_SERVER_URL: 'http://localhost:5173/',
      ...(mainJs ? { OW_APP_MAIN: mainJs } : { OW_START_URL: 'http://localhost:5173/' }),
      ...(workersDir ? { OW_APP_WORKERS: workersDir } : {}),
      ...(() => {
        // módulos stock (fs/path/…) + los nativos de la app, si los hay
        const dirs = [stockModulesPath(), modulesDir].filter(Boolean)
        return dirs.length ? { OW_MODULES_DIR: dirs.join(path.delimiter) } : {}
      })(),
    },
  })
  kernel.on('exit', (code) => {
    log(`kernel terminó (${code})`)
    vite.kill('SIGTERM')
    process.exit(code ?? 0)
  })
  process.on('SIGINT', () => {
    kernel.kill('SIGTERM')
    vite.kill('SIGTERM')
    process.exit(0)
  })
}

async function cmdBuild() {
  const cwd = process.cwd()
  log('vite build…')
  const code = await runProc('npx', ['vite', 'build'], { cwd, shell: process.platform === 'win32' })
  if (code !== 0) die('vite build falló')

  const mainJs = prepareMain(cwd, path.join(cwd, 'dist'))

  const workersDir = prepareWorkers(cwd, path.join(cwd, 'dist'))
  if (workersDir) log(`workers: ${path.relative(cwd, workersDir)}`)

  const nativeDir = path.join(cwd, 'native')
  if (fs.existsSync(nativeDir)) {
    const out = path.join(cwd, 'dist', 'modules')
    const r = spawnSync('npx', ['owear-build-native'], {
      cwd,
      stdio: 'inherit',
      shell: process.platform === 'win32',
      env: { ...process.env, OW_MODULES_OUT: out },
    })
    if (r.status !== 0) die('falló la compilación de native/*.cpp')
  }
  log('build lista en dist/')
  const runMods = [
    stockModulesPath(),
    fs.existsSync(path.join(cwd, 'dist', 'modules')) ? path.join(cwd, 'dist', 'modules') : '',
  ]
    .filter(Boolean)
    .join(path.delimiter)
  console.log(
    `  OW_ASSETS_DIR=dist${mainJs ? ' OW_APP_MAIN=dist/main.js' : ''}` +
      `${workersDir ? ' OW_APP_WORKERS=dist/workers' : ''}` +
      `${runMods ? ` OW_MODULES_DIR="${runMods}"` : ''} ./owear`
  )
}

main().catch((e) => die(e?.stack ?? String(e)))
