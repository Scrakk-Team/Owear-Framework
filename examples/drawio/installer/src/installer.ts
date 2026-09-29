// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/installer.ts — UI del instalador (vanilla). Corre en el renderer y usa
// la API nativa `installer` vía `window.ow` (inyectado por el kernel).
//
// Personalízalo a gusto (React, Vue, Tailwind…): esto es sólo el default.

import './style.css'

declare global {
  interface Window {
    __owWindowId: number
    ow: { invoke<T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T> }
  }
}

const $ = <T extends HTMLElement = HTMLElement>(sel: string): T =>
  document.querySelector(sel) as T
const invoke = <T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T> =>
  window.ow.invoke(module, fn, ...args) as Promise<T>

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

function setStatus(msg: string, kind: 'ok' | 'err' | '' = ''): void {
  const el = $('#status')
  el.textContent = msg
  el.dataset.kind = kind
}

async function boot(): Promise<void> {
  // controles de ventana (titlebar propia)
  const id = window.__owWindowId
  $('#win-close').addEventListener('click', () => void invoke('ow-window', 'close', id))

  const info = await invoke<InstallerInfo>('installer', 'info').catch(() => ({}))
  const bridge = await invoke<Bridge | null>('installer', 'bridge').catch(() => null)
  const t: Target = bridge?.targets?.[platform] ?? {}
  mode = t.layout ?? 'minimal'

  const name = info.appName ?? bridge?.app?.name ?? 'la app'
  $('#summary').textContent =
    `${name} ${info.version ?? bridge?.app?.version ?? ''} — modo ${mode} · ${platform}`

  const value = t.dir ?? (info.appName ? `~/opt/${info.appName.toLowerCase()}` : '')
  ;($('#dir') as HTMLInputElement).value = value

  await refreshPlan()
  $('#install').removeAttribute('disabled')
}

async function refreshPlan(): Promise<void> {
  plan = await invoke<PayloadEntry[]>('installer', 'plan', {
    mode,
    layout: mode === 'minimal' ? 'flat' : 'tree',
    order: (await invoke<Bridge | null>('installer', 'bridge').catch(() => null))?.order,
  })
  const total = plan.reduce((n, e) => n + e.size, 0)
  $('#count').textContent = `${plan.length} entradas · ${(total / 1024).toFixed(1)} KiB`
  $('#plan').textContent = plan
    .slice(0, 300)
    .map((e) => `${e.dir ? '📁' : '📄'} ${e.dst}${e.size ? `  (${e.size} B)` : ''}`)
    .join('\n')
}

async function pickDir(): Promise<void> {
  try {
    const dir = await invoke<string | null>('dialog', 'open', 'openDirectory', 'Elegir carpeta')
    if (dir) ($('#dir') as HTMLInputElement).value = dir
  } catch {
    /* cancelado o sin picker */
  }
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
      const exec = plan.find((e) => !e.dir && !e.dst.includes('/'))?.dst ?? plan[0]?.dst
      await invoke('installer', 'shortcuts', { execPath: `${dir}/${exec ?? ''}` }).catch(() => false)
    }
    if (($('#launch') as HTMLInputElement).checked) {
      const exec = plan.find((e) => !e.dir && !e.dst.includes('/'))?.dst ?? plan[0]?.dst
      await invoke('installer', 'launch', { path: `${dir}/${exec ?? ''}` }).catch(() => false)
      const v = await invoke<{ elevated: boolean }>('installer', 'elevate').catch(() => ({ elevated: false }))
      void v
      await invoke('ow-window', 'close', window.__owWindowId)
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
$('#cancel').addEventListener('click', () => void invoke('ow-window', 'close', window.__owWindowId))

void boot()
