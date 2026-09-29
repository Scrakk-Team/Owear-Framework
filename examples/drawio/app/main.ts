// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// app/main.ts — proceso principal del port de draw.io a Owear.
//
// Sustituye al `src/main/electron.js` (4541 líneas) de drawio-desktop. La webapp
// de draw.io NO cambia: habla por `window.electron.request(...)` (ver
// owear-preload.js) y aquí atendemos cada `action` del switch `rendererReq`.
//
// Contrato replicado 1:1 con drawio-desktop (mismas acciones y formatos).

import { app, BrowserWindow, dialog, invokeNative } from '@owear/core'
import * as fs from 'node:fs'
import * as fsp from 'node:fs/promises'
import * as os from 'node:os'
import * as path from 'node:path'

interface Req {
  action: string
  [k: string]: unknown
}

function documentsFolder(): string {
  try {
    return path.join(os.homedir(), 'Documents')
  } catch {
    return '.'
  }
}

app.whenReady().then(() => {
  const win = new BrowserWindow({
    title: 'draw.io',
    width: 1280,
    height: 800,
    minWidth: 720,
    minHeight: 480,
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })
  win.on('closed', () => app.quit())

  // ── drawio.req: réplica del switch de `rendererReq` ───────────────────────
  app.handle('drawio.req', async (msg: Req) => {
    switch (msg.action) {
      case 'saveFile': {
        const f = msg.fileObject as { path?: string; encoding?: string } | null
        if (!f?.path) throw new Error('bad arg: fileObject.path')
        const enc = (msg.defEnc as string) || f.encoding || 'utf8'
        await fsp.writeFile(f.path, msg.data as string, enc as BufferEncoding)
        return await fsp.stat(f.path)
      }

      case 'writeFile':
        await fsp.writeFile(msg.path as string, msg.data as string, ((msg.enc as string) || 'utf8') as BufferEncoding)
        return null

      case 'readFile':
        return await fsp.readFile(msg.filename as string, ((msg.encoding as string) || 'utf8') as BufferEncoding)

      case 'dirname':
        return path.dirname(msg.path as string)

      case 'fileStat':
        return await fsp.stat(msg.file as string)

      case 'isFileWritable':
        try {
          await fsp.access(msg.file as string, fs.constants.W_OK)
          return true
        } catch {
          return false
        }

      case 'checkFileExists': {
        const p = path.join(...((msg.pathParts as string[]) ?? []))
        return { exists: fs.existsSync(p), path: p }
      }

      case 'deleteFile':
        try {
          await fsp.unlink(msg.file as string)
        } catch {
          /* ignore */
        }
        return null

      case 'getDocumentsFolder':
        return documentsFolder()

      case 'showOpenDialog': {
        const r = await dialog.showOpenDialog({
          defaultPath: msg.defaultPath as string | undefined,
          filters: msg.filters as never,
          properties: (msg.properties as never) ?? ['openFile'],
        })
        return { canceled: r?.canceled ?? true, filePaths: r?.filePaths ?? [] }
      }

      case 'showSaveDialog': {
        const r = await dialog.showSaveDialog({
          defaultPath: msg.defaultPath as string | undefined,
          filters: msg.filters as never,
        })
        return { canceled: r?.canceled ?? true, filePath: r?.filePath ?? null }
      }

      case 'windowAction': {
        const m = msg.method as string
        if (m === 'minimize') win.minimize()
        else if (m === 'maximize') win.maximize()
        else if (m === 'unmaximize') win.unmaximize()
        else if (m === 'close') win.close()
        else if (m === 'isMaximized') return win.isMaximized()
        return null
      }

      case 'openExternal':
        await invokeNative('shell', 'openExternal', msg.url as string)
        return true

      case 'clipboardAction': {
        const m = msg.method as string
        if (m === 'writeText') {
          await invokeNative('clipboard', 'writeText', msg.data as string)
          return null
        }
        if (m === 'readText') return await invokeNative<string>('clipboard', 'readText')
        return null
      }

      case 'isFullscreen':
        return win.isFullScreen()

      case 'isPluginsEnabled':
        return true

      case 'getLocalFonts':
        return [] // TODO: fc-list (Linux) / enumeración por SO

      case 'exit':
        app.quit()
        return null

      // Aún no portados (borradores/backup/watcher): degradan sin romper.
      case 'saveDraft':
      case 'getFileDrafts':
      case 'getBkpFile':
      case 'watchFile':
      case 'unwatchFile':
        return null

      default:
        console.debug('[drawio] acción no soportada:', msg.action)
        return null
    }
  })

  // drawio.send: mensajes fire-and-forget del renderer (menús, telemetría…).
  app.handle('drawio.send', async (_action: string, _args: unknown) => null)
})
