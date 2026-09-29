// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
import { defineConfig } from 'vite'
import owear from '@owear/vite-plugin'

export default defineConfig({
  plugins: [owear()],
  base: './',
  build: {
    outDir: 'dist',
  },
})
