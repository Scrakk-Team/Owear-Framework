// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/tray/src/tray_mac.mm — stub (macOS fuera de 0.1.x).
//
#include "ow/Module.h"
#include "ow_api.h"

namespace tray {
using ow::Module::RespondOk;
static void ok(const ow_request_t*, ow_response_t* res) { RespondOk(res, "null"); }
void create(const ow_request_t*, ow_response_t* res) { RespondOk(res, "null"); }
void setImage(const ow_request_t*, ow_response_t* res) { ok(nullptr, res); }
void setPressedImage(const ow_request_t*, ow_response_t* res) { ok(nullptr, res); }
void setToolTip(const ow_request_t*, ow_response_t* res) { ok(nullptr, res); }
void setTitle(const ow_request_t*, ow_response_t* res) { ok(nullptr, res); }
void setContextMenu(const ow_request_t*, ow_response_t* res) { ok(nullptr, res); }
void popupContextMenu(const ow_request_t*, ow_response_t* res) { ok(nullptr, res); }
void destroy(const ow_request_t*, ow_response_t* res) { ok(nullptr, res); }
} // namespace tray

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"create", &tray::create},
        {"setImage", &tray::setImage},
        {"setPressedImage", &tray::setPressedImage},
        {"setToolTip", &tray::setToolTip},
        {"setTitle", &tray::setTitle},
        {"setContextMenu", &tray::setContextMenu},
        {"popupContextMenu", &tray::popupContextMenu},
        {"destroy", &tray::destroy},
    };
    static const ow_module_desc_t d{"tray", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
