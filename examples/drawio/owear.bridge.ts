// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// owear.bridge.ts — contrato del instalador de draw.io (ver D1).
//
// `ow build installer` (o build-installer.mjs) lo compila y lo embebe como
// bridge.json en el binario instalador. El instalador colocará draw.io como
// árbol de carpetas (layout) con un launcher que fija OW_ASSETS_DIR/OW_APP_MAIN.

import { defineBridge } from '@owear/core'

export default defineBridge({
  app: {
    id: 'com.jgraph.drawio',
    name: 'draw.io',
    version: '1.0.0',
    publisher: 'jgraph',
    icon: 'webapp/favicon.ico',
  },
  targets: {
    linux: {
      layout: 'layout',
      format: ['binary'],
      preset: 'flat',
      dir: '~/opt/drawio',
      shortcuts: ['desktop', 'menu'],
      scope: 'user',
    },
    win: {
      layout: 'layout',
      format: ['exe'],
      preset: 'flat',
      dir: '%LOCALAPPDATA%\\drawio',
      shortcuts: ['startMenu', 'desktop'],
      scope: 'user',
    },
  },
  order: ['kernel', 'modules:stock', 'app:assets', 'app:main'],
  node: { mode: 'system' },
})
