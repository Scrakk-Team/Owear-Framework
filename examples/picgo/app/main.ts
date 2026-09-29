// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// app/main.ts — proceso principal (sidecar Node) del port de PicGo.
//
// El motor de subida es el CORE de PicGo (`picgo` en npm): aquí lo instanciamos
// y lo exponemos al renderer con `app.handle` (ow.invoke('node','call',…)).
// La UI (renderer) NO depende de Electron: es HTML/CSS/JS sobre Owear.

import { app, BrowserWindow } from '@owear/core'
import * as os from 'node:os'
import * as path from 'node:path'

type UploadResult = { imgUrl?: string; url?: string; error?: string }

let picgoPromise: Promise<any> | null = null

async function getPicgo(): Promise<any> {
  if (!picgoPromise) {
    picgoPromise = (async () => {
      const mod: any = await import('picgo')
      const PicGo = mod?.default?.default ?? mod?.default ?? mod?.PicGo ?? mod
      return new PicGo()
    })()
  }
  return picgoPromise
}

function configPath(): string {
  return path.join(os.homedir(), '.picgo', 'config.json')
}

app.whenReady().then(() => {
  // Rutas donde PicGo guarda su config (compatible con el PicGo de escritorio).
  app.handle('picgo.configPath', async () => configPath())

  // Subida: recibe rutas de fichero y devuelve las URLs resultantes.
  app.handle('picgo.upload', async (args: unknown) => {
    const paths = Array.isArray(args) ? (args as string[]) : [String(args)]
    try {
      const picgo = await getPicgo()
      const output: string[] = []
      await new Promise<void>((resolve) => {
        picgo.on('finished', (ctx: any) => {
          for (const it of ctx?.output ?? []) {
            const u = it?.imgUrl ?? it?.url
            if (u) output.push(String(u))
          }
          resolve()
        })
        picgo.on('failed', (ctx: any) => {
          output.push(`ERROR: ${ctx?.error?.message ?? String(ctx)}`)
          resolve()
        })
        picgo.upload(paths)
      })
      return output
    } catch (e) {
      return { error: e instanceof Error ? e.message : String(e) }
    }
  })

  const win = new BrowserWindow({
    title: 'PicGo · Owear',
    width: 940,
    height: 660,
    titleBarStyle: 'custom',
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })
  win.on('closed', () => app.quit())
})

export type { UploadResult }
