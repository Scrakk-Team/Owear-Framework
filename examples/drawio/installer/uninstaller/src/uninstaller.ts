// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/uninstaller.ts — UI del desinstalador (vanilla). Mismo bridge, pero
// invoca `installer.uninstall`. Detecta la instalación por el REGISTRO de Owear
// (no hace falta escribir la ruta ni usar módulos .owm).
// Estilos: tokens del Starter (ver ../../src/style.css).

import '../../src/style.css'

interface OwBridge {
  invoke<T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T>
}
const ow = (window as unknown as { ow: OwBridge }).ow
const wid = (window as unknown as { __owWindowId: number }).__owWindowId

const $ = <T extends HTMLElement = HTMLElement>(sel: string): T =>
  document.querySelector(sel) as T
const invoke = <T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T> =>
  ow.invoke<T>(module, fn, ...args)

interface StateResult {
  installed: boolean
  version?: string
  app?: string
  dir?: string
}
interface Info {
  appName?: string
  version?: string
  dir?: string
  installed?: boolean
}

function setStatus(msg: string, kind: 'ok' | 'err' | '' = ''): void {
  const el = $('#status')
  el.textContent = msg
  el.dataset.kind = kind
}

async function boot(): Promise<void> {
  $('#win-min').addEventListener('click', () => void invoke('ow-window', 'minimize', wid))
  $('#win-close').addEventListener('click', () => void invoke('ow-window', 'close', wid))

  const info = await invoke<Info>('installer', 'info').catch(() => ({}))
  const st = await invoke<StateResult>('installer', 'state', {}).catch(() => ({
    installed: false,
  }))
  const name = st.app ?? info.appName ?? 'La app'
  const dir = st.dir ?? info.dir ?? ''

  if (st.installed) {
    const v = st.version ?? info.version ?? ''
    $('#summary').textContent = `${name}${v ? ' ' + v : ''} está instalada${
      dir ? ' en ' + dir : ''
    }.`
  } else {
    $('#summary').textContent =
      'No se detectó ninguna instalación registrada. Elige la carpeta donde se instaló.'
  }
  ;($('#dir') as HTMLInputElement).value = dir
  $('#uninstall').removeAttribute('disabled')
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

async function doUninstall(): Promise<void> {
  const dir = ($('#dir') as HTMLInputElement).value.trim()
  const keepData = ($('#keepData') as HTMLInputElement).checked
  $('#uninstall').setAttribute('disabled', 'true')
  setStatus('Desinstalando…')
  try {
    // Si no hay dir, el builtin usa el registrado.
    const res = await invoke<{ removed: number }>(
      'installer',
      'uninstall',
      dir ? { dir, keepData } : { keepData },
    )
    setStatus(`Eliminados ${res.removed} ficheros.`, 'ok')
    setTimeout(() => void invoke('ow-window', 'close', wid), 900)
  } catch (e) {
    setStatus(e instanceof Error ? e.message : String(e), 'err')
    $('#uninstall').removeAttribute('disabled')
  }
}

$('#pick').addEventListener('click', () => void pickDir())
$('#uninstall').addEventListener('click', () => void doUninstall())
void boot()
