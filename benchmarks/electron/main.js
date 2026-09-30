// Copyright 2026 Owear Contributors — SPDX-License-Identifier: Apache-2.0
// benchmarks/electron/main.js — app mínima de benchmark (IPC + eventos).
const { app, BrowserWindow, ipcMain } = require('electron')
const fs = require('fs')
const path = require('path')

const T0 = Date.now()

const OUT = '/tmp/opencode/bench/out/electron.json'

app.commandLine.appendSwitch('no-sandbox')
app.commandLine.appendSwitch('disable-dev-shm-usage')
app.disableHardwareAcceleration()

ipcMain.handle('bench:echo', (_e, x) => x)
ipcMain.handle('bench:burst', (e, n) => {
  for (let i = 0; i < n; i++) e.sender.send('bench-tick', i)
  return null
})
ipcMain.handle('bench:ready', () => {
  process.stdout.write(`BENCH_READY internal=${Date.now() - T0}ms\n`)
  try {
    fs.writeFileSync(OUT.replace(/\.json$/, '.ready'), '1')
  } catch {}
  return null
})
ipcMain.handle('bench:report', (_e, json) => {
  fs.writeFileSync(OUT, json)
  return null
})
ipcMain.handle('bench:readfile', () => fs.readFileSync('/tmp/opencode/bench/blob.bin'))
ipcMain.handle('bench:big', (_e, n) => 'y'.repeat(n))
ipcMain.handle('bench:done', () => {
  setTimeout(() => app.quit(), 150)
  return null
})

app.whenReady().then(() => {
  const win = new BrowserWindow({
    width: 900,
    height: 700,
    webPreferences: { preload: path.join(__dirname, 'preload.js') },
  })
  win.loadFile('index.html')
})

app.on('window-all-closed', () => app.quit())
