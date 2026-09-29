// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
import { defineConfig } from 'vite'
import owear from '@owear/vite-plugin'

export default defineConfig({
  plugins: [owear()],
  base: './',
  build: {
    // `ow build installer` recoge este directorio como `ui/` del instalador.
    outDir: 'ui-dist',
    emptyOutDir: true,
  },
})
