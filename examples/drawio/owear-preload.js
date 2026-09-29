// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// owear-preload.js — ADAPTADOR Electron → Owear para draw.io.
//
// drawio-desktop habla con el main por `window.electron.request(msg, cb, err)`
// (preload → ipcRenderer 'rendererReq' → main responde 'mainResp'). En Owear no
// hay preload: el kernel inyecta `window.ow`. Este script reproduce la MISMA
// API `window.electron` sobre `window.ow` para que la webapp de draw.io no
// cambie. Se carga ANTES de js/main.js (ver setup.mjs).
(function () {
  var ow = window.ow
  if (!ow) {
    console.warn('[owear-preload] window.ow no disponible')
    return
  }

  function request(msg, callback, error) {
    ow.invoke('node', 'call', { fn: 'drawio.req', args: [msg] }).then(
      function (data) {
        if (callback) callback(data)
      },
      function (e) {
        if (error) error(String((e && e.message) || e), e)
      },
    )
  }

  window.electron = {
    // Mismo contrato que electron-preload.js de drawio-desktop.
    request: request,
    registerMsgListener: function (action, callback) {
      ow.on(action, callback)
    },
    sendMessage: function (action, args) {
      ow.invoke('node', 'call', { fn: 'drawio.send', args: [action, args] }).catch(function () {})
    },
    listenOnce: function (action, callback) {
      var off = ow.on(action, function (a) {
        try {
          if (typeof off === 'function') off()
        } catch (e) {}
        callback(a)
      })
    },
  }

  // drawio comprueba `window.process.type === 'renderer'` para activar el modo
  // escritorio (abrir/guardar por el main). Lo exponemos mínimo.
  window.process = { type: 'renderer', versions: (window.process && window.process.versions) || {} }
})()
