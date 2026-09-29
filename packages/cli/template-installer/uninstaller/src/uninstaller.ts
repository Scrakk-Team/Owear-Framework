// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/uninstaller.ts — UI del desinstalador (vanilla). Mismo bridge, pero
// invoca `installer.uninstall`.

import '../../src/style.css'

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

function setStatus(msg: string, kind: 'ok' | 'err' | '' = ''): void {
  const el = $('#status')
  el.textContent = msg
  el.dataset.kind = kind
}

async function boot(): Promise<void> {
  $('#win-close').addEventListener('click', () => void invoke('ow-window', 'close', window.__owWindowId))

  const info = await invoke<{ appName?: string; dir?: string }>('installer', 'info').catch(() => ({}))
  const st = await invoke<{ installed: boolean; version?: string; app?: string }>(
    'installer',
    'state',
    { dir: info.dir ?? '' },
  ).catch(() => ({ installed: false }))

  $('#summary').textContent = st.installed
    ? `${st.app ?? info.appName ?? 'La app'} ${st.version ?? ''} está instalada.`
    : 'No se detectó una instalación en el directorio por defecto; elígelo.'
  ;($('#dir') as HTMLInputElement).value = info.dir ?? ''
  $('#uninstall').removeAttribute('disabled')
}

async function pickDir(): Promise<void> {
  try {
    const dir = await invoke<string | null>('dialog', 'open', 'openDirectory', 'Elegir carpeta')
    if (dir) ($('#dir') as HTMLInputElement).value = dir
  } catch {
    /* cancelado */
  }
}

async function doUninstall(): Promise<void> {
  const dir = ($('#dir') as HTMLInputElement).value.trim()
  if (!dir) {
    setStatus('Elige la carpeta de instalación', 'err')
    return
  }
  $('#uninstall').setAttribute('disabled', 'true')
  setStatus('Desinstalando…')
  try {
    const res = await invoke<{ removed: number }>('installer', 'uninstall', {
      dir,
      keepData: ($('#keepData') as HTMLInputElement).checked,
    })
    setStatus(`Eliminados ${res.removed} ficheros.`, 'ok')
    setTimeout(() => void invoke('ow-window', 'close', window.__owWindowId), 900)
  } catch (e) {
    setStatus(e instanceof Error ? e.message : String(e), 'err')
    $('#uninstall').removeAttribute('disabled')
  }
}

$('#pick').addEventListener('click', () => void pickDir())
$('#uninstall').addEventListener('click', () => void doUninstall())
void boot()
