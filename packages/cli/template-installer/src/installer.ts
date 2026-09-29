// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/installer.ts — UI del instalador (vanilla). Corre en el renderer y usa
// SOLO la API nativa `installer` (builtin, siempre presente) vía `window.ow`.
// No depende de módulos .owm (dialog, etc.).
//
// Personalízalo a gusto (React, Vue, Tailwind…): esto es sólo el default.

import './style.css'

interface OwBridge {
  invoke<T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T>
}
const ow = (window as unknown as { ow: OwBridge }).ow
const wid = (window as unknown as { __owWindowId: number }).__owWindowId

const $ = <T extends HTMLElement = HTMLElement>(sel: string): T =>
  document.querySelector(sel) as T
const invoke = <T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T> =>
  ow.invoke<T>(module, fn, ...args)

interface PayloadEntry {
  rel: string
  dst: string
  size: number
  dir: boolean
}
interface InstallerInfo {
  appId?: string
  appName?: string
  version?: string
  publisher?: string
}
interface Target {
  layout?: 'minimal' | 'layout'
  dir?: string
  shortcuts?: string[]
}
interface Bridge {
  app?: { id?: string; name?: string; version?: string; publisher?: string }
  targets?: Record<string, Target>
  order?: string[]
}

const platform: string = (() => {
  const p = navigator.userAgent.toLowerCase()
  return p.includes('win') ? 'win' : p.includes('mac') ? 'mac' : 'linux'
})()

let mode: 'minimal' | 'layout' = 'minimal'
let plan: PayloadEntry[] = []
let busy = false

function fmtSize(bytes: number): string {
  if (!bytes) return ''
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`
  return `${(bytes / 1024 / 1024).toFixed(1)} MiB`
}

function setStatus(msg: string, kind: 'ok' | 'err' | '' = ''): void {
  const el = $('#status')
  el.textContent = msg
  el.dataset.kind = kind
}

async function boot(): Promise<void> {
  $('#win-min').addEventListener('click', () => void invoke('ow-window', 'minimize', wid))
  $('#win-max').addEventListener('click', () => void invoke('ow-window', 'maximize', wid))
  $('#win-close').addEventListener('click', () => void invoke('ow-window', 'close', wid))

  const info = await invoke<InstallerInfo>('installer', 'info').catch(() => ({}))
  const bridge = await invoke<Bridge | null>('installer', 'bridge').catch(() => null)
  const t: Target = bridge?.targets?.[platform] ?? {}
  mode = t.layout ?? 'minimal'

  const name = info.appName ?? bridge?.app?.name ?? 'la app'
  const version = info.version ?? bridge?.app?.version ?? ''
  $('#summary').textContent =
    `Se instalará ${name}${version ? ` ${version}` : ''} · modo ${mode} · ${platform}`

  // Destino: preferimos el del bridge si es ABSOLUTO; si no, el default real.
  let value = t.dir ?? ''
  if (!value.startsWith('/')) {
    value = await invoke<string>('installer', 'defaultDir').catch(() => '')
  }
  ;($('#dir') as HTMLInputElement).value = value

  await refreshPlan()
  $('#install').removeAttribute('disabled')
}

async function refreshPlan(): Promise<void> {
  const bridge = await invoke<Bridge | null>('installer', 'bridge').catch(() => null)
  plan = await invoke<PayloadEntry[]>('installer', 'plan', {
    mode,
    layout: mode === 'minimal' ? 'flat' : 'tree',
    order: bridge?.order,
  })
  const total = plan.reduce((n, e) => n + e.size, 0)
  $('#count').textContent = `${plan.length} · ${fmtSize(total)}`
  $('#plan').textContent = plan
    .slice(0, 300)
    .map((e) => `${e.dst}${e.dir ? '/' : ''}${e.size ? `  ${fmtSize(e.size)}` : ''}`)
    .join('\n')
}

async function pickDir(): Promise<void> {
  const current = ($('#dir') as HTMLInputElement).value.trim()
  try {
    // Selector NATIVO del builtin installer (no usa el módulo `dialog`).
    const dir = await invoke<string | null>('installer', 'chooseDir', {
      title: 'Elegir carpeta de instalación',
      defaultPath: current,
    })
    if (dir) ($('#dir') as HTMLInputElement).value = dir
  } catch {
    /* cancelado */
  }
}

function targetExec(dir: string): string {
  const top = plan.find((e) => !e.dir && !e.dst.includes('/')) ?? plan[0]
  return top ? `${dir}/${top.dst}` : ''
}

async function doInstall(): Promise<void> {
  if (busy) return
  busy = true
  const dir = ($('#dir') as HTMLInputElement).value.trim()
  if (!dir) {
    setStatus('Elige un directorio de instalación', 'err')
    busy = false
    return
  }
  $('#install').setAttribute('disabled', 'true')
  $('#cancel').removeAttribute('hidden')
  $('#bar').removeAttribute('hidden')
  setStatus('Instalando…')

  try {
    const res = await invoke<{ files: number; dir: string }>('installer', 'install', {
      dir,
      mode,
      layout: mode === 'minimal' ? 'flat' : 'tree',
    })
    $('#bar').value = 100
    setStatus(`Instalado en ${res.dir} (${res.files} ficheros)`, 'ok')

    if (($('#shortcut') as HTMLInputElement).checked) {
      await invoke('installer', 'shortcuts', { execPath: targetExec(res.dir) }).catch(() => false)
    }
    if (($('#launch') as HTMLInputElement).checked) {
      await invoke('installer', 'launch', { path: targetExec(res.dir) }).catch(() => false)
      await invoke('ow-window', 'close', wid)
    }
  } catch (e) {
    setStatus(e instanceof Error ? e.message : String(e), 'err')
    $('#install').removeAttribute('disabled')
  } finally {
    busy = false
    $('#cancel').setAttribute('hidden', 'true')
  }
}

$('#pick').addEventListener('click', () => void pickDir())
$('#install').addEventListener('click', () => void doInstall())
$('#cancel').addEventListener('click', () => void invoke('ow-window', 'close', wid))

void boot()
