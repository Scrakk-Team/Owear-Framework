// Copyright 2026 Owear Contributors — SPDX-License-Identifier: Apache-2.0
const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('bench', {
  invoke: (x) => ipcRenderer.invoke('bench:echo', x),
  on: (ev, cb) => ipcRenderer.on(ev, (_e, data) => cb(data)),
  burst: (n) => ipcRenderer.invoke('bench:burst', n),
  ready: () => ipcRenderer.invoke('bench:ready'),
  report: (json) => ipcRenderer.invoke('bench:report', json),
  readfile: () => ipcRenderer.invoke('bench:readfile'),
  done: () => ipcRenderer.invoke('bench:done'),
})
