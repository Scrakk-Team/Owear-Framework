// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Fixture de worker estilo Electron: usa `process.parentPort` tal cual, para
// verificar que el shim de Owear lo emula sin tocar el worker.

process.parentPort.on('message', (event) => {
  process.parentPort.postMessage({ echo: event.data })
})
