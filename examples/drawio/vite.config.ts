// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// vite.config.ts — draw.io NO es un proyecto vite: su renderer es la webapp
// estática de draw.io. Para poder usar el comando estándar `ow build` /
// `ow build installer`, este build SOLO copia `webapp/` → `dist/` (y el plugin
// de Owear inyecta el bridge). `ow build` añade después `dist/main.js`.
import { cpSync, existsSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { defineConfig, type Plugin } from 'vite'
import owear from '@owear/vite-plugin'

const __dirname = dirname(fileURLToPath(import.meta.url))

const copyWebapp: Plugin = {
  name: 'drawio-copy-webapp',
  apply: 'build',
  closeBundle() {
    const webapp = resolve(__dirname, 'webapp')
    if (!existsSync(webapp)) {
      throw new Error('falta webapp/ — ejecuta: node setup.mjs')
    }
    cpSync(webapp, resolve(__dirname, 'dist'), { recursive: true })
  },
}

export default defineConfig({
  plugins: [owear(), copyWebapp],
  build: { outDir: 'dist', emptyOutDir: true },
})
