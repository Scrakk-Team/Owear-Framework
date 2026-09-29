// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// vite.config.ts — build del RENDERER del port de PicGo a Owear.
//
// Equivale a la sección `renderer` de electron.vite.config.ts, pero como app
// Owear (un solo vite build): el kernel sirve `dist/` vía app:// y el sidecar
// Node (app/main.ts) sustituye al main de Electron.
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import { tanstackRouter } from '@tanstack/router-plugin/vite'
import { resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import owear from '@owear/vite-plugin'
import { i18nTypesPlugin } from './scripts/vite-plugin-i18n-types'

const __dirname = fileURLToPath(new URL('.', import.meta.url))

const alias = {
  '@': resolve(__dirname, 'src/renderer'),
  '~': resolve(__dirname, 'src'),
  '#': resolve(__dirname, 'src/universal'),
  root: resolve(__dirname, '.'),
  apis: resolve(__dirname, 'src/main/apis'),
  '@core': resolve(__dirname, 'src/main/apis/core'),
}

export default defineConfig({
  root: resolve(__dirname, 'src/renderer'),
  publicDir: resolve(__dirname, 'src/renderer/public'),
  base: './',
  resolve: { alias },
  plugins: [
    tanstackRouter({
      target: 'react',
      autoCodeSplitting: true,
      routesDirectory: './routes',
      generatedRouteTree: './routeTree.gen.ts',
    }),
    react(),
    i18nTypesPlugin(),
    tailwindcss(),
    owear(),
  ],
  build: {
    outDir: resolve(__dirname, 'dist'),
    emptyOutDir: true,
  },
})
