// ─────────────────────────────────────────────────────────────────────────────
// GENERADO por tools/gen-apis.mjs — NO EDITAR A MANO.
// Fuente de verdad: api/<nombre>/owear.module.json (kind=builtin)
// Regenerar: node tools/gen-apis.mjs
// ─────────────────────────────────────────────────────────────────────────────

#include "../Bridge/Dispatcher.hpp"
#include "ow_api.h"

namespace ow::internal {

// Factories declaradas en src/Core/… o api/…/src/….
#if defined(OW_BUILTINS_GTK)
const ow_module_desc_t* AppModuleDescriptor();
#endif
#if defined(OW_BUILTINS_GTK)
const ow_module_desc_t* CrashReporterDescriptor();
#endif
#if defined(OW_BUILTINS_GTK) || defined(OW_PLATFORM_WIN)
const ow_module_desc_t* InstallerModuleDescriptor();
#endif
const ow_module_desc_t* NodeBridgeDescriptorImpl();
#if defined(OW_BUILTINS_GTK)
const ow_module_desc_t* SessionDescriptor();
#endif
#if defined(OW_BUILTINS_GTK) || defined(OW_PLATFORM_WIN)
const ow_module_desc_t* WebviewModuleDescriptor();
#endif
const ow_module_desc_t* WindowModuleDescriptorImpl();
#if defined(OW_BUILTINS_GTK)
const ow_module_desc_t* WindowExtrasDescriptor();
#endif
#if defined(OW_PLATFORM_WIN)
const ow_module_desc_t* WindowExtrasDescriptorWin();
#endif
#if defined(__APPLE__)
const ow_module_desc_t* WindowExtrasDescriptorMac();
#endif

void RegisterGeneratedBuiltins() {
#if defined(OW_BUILTINS_GTK)
    Dispatcher::Get().RegisterModule(AppModuleDescriptor(), "builtin:app");
#endif
#if defined(OW_BUILTINS_GTK)
    Dispatcher::Get().RegisterModule(CrashReporterDescriptor(), "builtin:crashreporter");
#endif
#if defined(OW_BUILTINS_GTK) || defined(OW_PLATFORM_WIN)
    Dispatcher::Get().RegisterModule(InstallerModuleDescriptor(), "builtin:installer");
#endif
    Dispatcher::Get().RegisterModule(NodeBridgeDescriptorImpl(), "builtin:node");
#if defined(OW_BUILTINS_GTK)
    Dispatcher::Get().RegisterModule(SessionDescriptor(), "builtin:session");
#endif
#if defined(OW_BUILTINS_GTK) || defined(OW_PLATFORM_WIN)
    Dispatcher::Get().RegisterModule(WebviewModuleDescriptor(), "builtin:webview");
#endif
    Dispatcher::Get().RegisterModule(WindowModuleDescriptorImpl(), "builtin:ow-window");
#if defined(OW_BUILTINS_GTK)
    Dispatcher::Get().RegisterModule(WindowExtrasDescriptor(), "builtin:window");
#endif
#if defined(OW_PLATFORM_WIN)
    Dispatcher::Get().RegisterModule(WindowExtrasDescriptorWin(), "builtin:window");
#endif
#if defined(__APPLE__)
    Dispatcher::Get().RegisterModule(WindowExtrasDescriptorMac(), "builtin:window");
#endif
}

} // namespace ow::internal
