// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/theme/src/theme_mac.mm — nativeTheme en macOS (stub; macOS fuera de 0.1.x).
//
#include "ow/Module.h"
#include "ow_api.h"

#include <string>

namespace th {

using ow::Module::RespondOk;

static std::string g_source = "system";

void get(const ow_request_t*, ow_response_t* res) {
    RespondOk(res, "{\"dark\":false,\"source\":\"system\",\"highContrast\":false}");
}
void isDark(const ow_request_t*, ow_response_t* res) { RespondOk(res, "false"); }
void setSource(const ow_request_t* req, ow_response_t* res) {
    (void)req;
    RespondOk(res, "null");
}
void watch(const ow_request_t*, ow_response_t* res) { RespondOk(res, "null"); }
void unwatch(const ow_request_t*, ow_response_t* res) { RespondOk(res, "null"); }
(void)g_source;

} // namespace th

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"get", &th::get},             {"isDark", &th::isDark},
        {"setSource", &th::setSource}, {"watch", &th::watch},
        {"unwatch", &th::unwatch},
    };
    static const ow_module_desc_t d{"theme", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
