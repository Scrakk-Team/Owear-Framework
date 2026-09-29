// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/renderer.ts — UI del port de PicGo (vanilla). Puerta a Node vía
// `ow.invoke('node','call',{ fn, args })` (el sidecar usa el core de PicGo).

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

let queue: string[] = []

function base(p: string): string {
  const parts = p.split(/[\\/]/)
  return parts[parts.length - 1] || p
}

function renderQueue(): void {
  const ul = $('#queue')
  ul.innerHTML = ''
  for (const f of queue) {
    const li = document.createElement('li')
    li.textContent = base(f)
    li.title = f
    ul.appendChild(li)
  }
  $('#count').textContent = String(queue.length)
  ;($('#upload') as HTMLButtonElement).disabled = queue.length === 0
}

function addResults(items: unknown): void {
  const ul = $('#results')
  const arr = Array.isArray(items) ? items : [items]
  for (const it of arr) {
    const url = String(it)
    const li = document.createElement('li')
    if (url.startsWith('ERROR')) {
      li.className = 'err'
      li.textContent = url
    } else {
      li.innerHTML = `<a href="${url}" target="_blank" rel="noreferrer">${url}</a>`
      const copy = document.createElement('button')
      copy.className = 'copy'
      copy.textContent = 'Copiar'
      copy.addEventListener('click', () => {
        navigator.clipboard?.writeText(url)
        copy.textContent = '¡Copiado!'
        setTimeout(() => (copy.textContent = 'Copiar'), 900)
      })
      li.appendChild(copy)
    }
    ul.prepend(li)
  }
}

async function pick(): Promise<void> {
  try {
    const r = await invoke<{ canceled: boolean; filePaths?: string[] }>('dialog', 'showOpenDialog', {
      properties: ['openFile', 'multiSelections'],
    })
    if (r && !r.canceled && r.filePaths?.length) {
      queue = queue.concat(r.filePaths)
      renderQueue()
    }
  } catch (e) {
    addResults(`ERROR: ${e instanceof Error ? e.message : String(e)}`)
  }
}

async function upload(): Promise<void> {
  if (!queue.length) return
  const files = queue.slice()
  ;($('#upload') as HTMLButtonElement).disabled = true
  ;($('#upload') as HTMLButtonElement).textContent = 'Subiendo…'
  try {
    const out = await invoke<unknown>('node', 'call', { fn: 'picgo.upload', args: files })
    addResults(out)
    queue = []
    renderQueue()
  } catch (e) {
    addResults(`ERROR: ${e instanceof Error ? e.message : String(e)}`)
  } finally {
    ;($('#upload') as HTMLButtonElement).textContent = 'Subir'
    ;($('#upload') as HTMLButtonElement).disabled = queue.length === 0
  }
}

function init(): void {
  const id = wid
  $('#win-min').addEventListener('click', () => void invoke('ow-window', 'minimize', id))
  $('#win-close').addEventListener('click', () => void invoke('ow-window', 'close', id))
  $('#pick').addEventListener('click', () => void pick())
  $('#upload').addEventListener('click', () => void upload())
  $('#clear').addEventListener('click', () => {
    queue = []
    renderQueue()
  })

  // Drag & drop (los navegadores embebidos exponen `path` en File).
  const drop = $('#drop')
  drop.addEventListener('dragover', (e) => {
    e.preventDefault()
    drop.classList.add('over')
  })
  drop.addEventListener('dragleave', () => drop.classList.remove('over'))
  drop.addEventListener('drop', (e) => {
    e.preventDefault()
    drop.classList.remove('over')
    const files = Array.from((e as DragEvent).dataTransfer?.files ?? [])
    const paths = files.map((f) => (f as unknown as { path?: string }).path).filter(Boolean) as string[]
    if (paths.length) {
      queue = queue.concat(paths)
      renderQueue()
    }
  })

  void invoke<string>('node', 'call', { fn: 'picgo.configPath', args: [] })
    .then((p) => {
      if (p) $('#cfg').textContent = p
    })
    .catch(() => {})

  renderQueue()
}

init()
