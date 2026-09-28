// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core/src/ipc/node.ts — puente Node (renderer ↔ main).
import { channel } from '../channel.js'

// ── Puente Node (renderer ↔ main) ───────────────────────────────────────────
// El renderer llama `ow.invoke('node','call',{fn,args})`; el kernel lo reenvía
// al main por el control socket; aquí se despacha al handler registrado con
// app.handle y se responde con `node.respond`. Es OPT-IN: solo para features
// que necesitan Node (p. ej. el extension host).
export type NodeHandler = (...args: any[]) => unknown | Promise<unknown>
/** Handler con contexto: recibe la ventana de origen y luego los args. */
export type ContextNodeHandler = (
  context: { windowId: number },
  ...args: any[]
) => unknown | Promise<unknown>
export const nodeHandlers = new Map<string, NodeHandler>()
export const nodeContextHandlers = new Map<string, ContextNodeHandler>()

channel.on('node.request', (params: any) => {
  const { reqId, fn, args, windowId } = params ?? {}
  const handler = nodeHandlers.get(fn)
  const ctxHandler = nodeContextHandlers.get(fn)
  const argsArr = Array.isArray(args) ? args : args === undefined ? [] : [args]
  Promise.resolve()
    .then(() =>
      ctxHandler
        ? ctxHandler({ windowId: typeof windowId === 'number' ? windowId : 0 }, ...argsArr)
        : handler
          ? handler(...argsArr)
          : undefined
    )
    .then(
      (result) => channel.call('node.respond', { reqId, ok: true, result }),
      (err) =>
        channel.call('node.respond', {
          reqId,
          ok: false,
          result: { message: err instanceof Error ? err.message : String(err) },
        })
    )
    .catch(() => undefined)
})


