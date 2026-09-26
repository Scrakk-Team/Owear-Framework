// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/NodeBridge.cpp — builtin "node": puente al proceso principal (Node).
//
// El renderer puede llamar handlers registrados en `app/main.ts` con
// `ow.invoke('node', 'call', { fn, args })` (app.handle en el SDK) y recibir
// eventos con `ow.on(name, …)` (app.send). Es OPT-IN: solo lo usan las features
// que necesitan Node (p. ej. el extension host). Los caminos calientes siguen
// yendo directo renderer → kernel → módulo nativo.
//
// La resolución es ASÍNCRONA: el bridge intercepta `node/call` en
// Window_common.cpp y lo reenvía por el control socket al main; el main
// responde con `node.respond`. Este descriptor existe para el registro
// (module.list / module.info) y la documentación; el `call` real lo gestiona el
// bridge, no el dispatcher.
#include "ow_api.h"
#include "ow/Module.h"

namespace ow {
namespace {

void callFn(const ow_request_t*, ow_response_t* res) {
    Module::RespondError(
        res,
        "node.call es asíncrono: lo resuelve el bridge (node.respond del main)");
}

const ow_fn_entry_t kFns[] = {{"call", &callFn}};
const ow_module_desc_t kDesc{"node", "0.1.0", kFns, 1};

} // namespace

namespace internal {
const ow_module_desc_t* NodeBridgeDescriptorImpl() { return &kDesc; }
} // namespace internal

} // namespace ow
