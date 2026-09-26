// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/webview/src/webview.cpp — builtin "webview": crea y controla WebViews
// EMBEBIDAS dentro de la ventana invocante (cada una con su propio proceso
// WebKit). Linux de momento. Mismo ABI que el resto de builtins.
//
//   ow.invoke('webview', 'create', { url, x, y, width, height })
//   ow.invoke('webview', 'load',    [id, url])
//   ow.invoke('webview', 'back',    [id])
//   ... (ver api/webview/owear.module.json)
//
// Los eventos van al renderer: webview.loadChanged {id,state,url},
// webview.urlChanged {id,url}, webview.titleChanged {id,title},
// webview.loadFailed {id,url,message}.
#include "../../../src/Control/ControlServer.hpp"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Window.h"

#include <string>

namespace ow {
namespace {

using ow::json::Array;
using ow::json::Value;

Window* WindowFromReq(const ow_request_t* req) {
    auto it = LiveWindows().find(static_cast<WindowId>(req->window_id));
    return it == LiveWindows().end() ? nullptr : it->second;
}

void create(const ow_request_t* req, ow_response_t* res) {
    Window* w = WindowFromReq(req);
    if (!w) return Module::RespondError(res, "ventana no encontrada");
    auto parsed = json::Parse(std::string_view(req->json, req->json_len));
    std::string opts = "{}";
    if (parsed.value && parsed.value->IsArray() &&
        !parsed.value->AsArray().empty())
        opts = parsed.value->AsArray()[0].Serialize();
    const std::string r = w->CreateWebview(opts);
    if (r.empty())
        return Module::RespondError(res, "webviews no soportadas en esta plataforma");
    Module::RespondOk(res, r.c_str());
}

/// Comando `[webviewId, ...rest]` → Window::WebviewCommand(id, op, restJson).
void doCommand(const ow_request_t* req, ow_response_t* res, const char* op) {
    Window* w = WindowFromReq(req);
    if (!w) return Module::RespondError(res, "ventana no encontrada");
    auto parsed = json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsNumber())
        return Module::RespondError(res, "se espera [webviewId, ...]");
    const auto& arr = parsed.value->AsArray();
    const uint32_t id = static_cast<uint32_t>(arr[0].AsInt());
    Array rest;
    for (size_t i = 1; i < arr.size(); ++i) rest.push_back(arr[i]);
    const std::string restJson = Value(std::move(rest)).Serialize();
    const std::string r = w->WebviewCommand(id, op, restJson);
    if (r.empty())
        return Module::RespondError(res, "webviews no soportadas en esta plataforma");
    Module::RespondOk(res, r.c_str());
}

#define OW_WV_CMD(fn, opname)                                     \
    void fn(const ow_request_t* req, ow_response_t* res) {        \
        doCommand(req, res, opname);                              \
    }

OW_WV_CMD(destroy, "destroy")
OW_WV_CMD(setBounds, "setBounds")
OW_WV_CMD(load, "load")
OW_WV_CMD(back, "back")
OW_WV_CMD(forward, "forward")
OW_WV_CMD(reload, "reload")
OW_WV_CMD(stop, "stop")
OW_WV_CMD(canBack, "canBack")
OW_WV_CMD(canForward, "canForward")
OW_WV_CMD(getURL, "getURL")
OW_WV_CMD(getTitle, "getTitle")
OW_WV_CMD(eval, "eval")
OW_WV_CMD(setVisible, "setVisible")
OW_WV_CMD(setZoom, "setZoom")
OW_WV_CMD(devtools, "devtools")
OW_WV_CMD(findInPage, "findInPage")
OW_WV_CMD(findStop, "findStop")

const ow_fn_entry_t kFns[] = {
    {"create", &create},
    {"destroy", &destroy},
    {"setBounds", &setBounds},
    {"load", &load},
    {"back", &back},
    {"forward", &forward},
    {"reload", &reload},
    {"stop", &stop},
    {"canBack", &canBack},
    {"canForward", &canForward},
    {"getURL", &getURL},
    {"getTitle", &getTitle},
    {"eval", &eval},
    {"setVisible", &setVisible},
    {"setZoom", &setZoom},
    {"devtools", &devtools},
    {"findInPage", &findInPage},
    {"findStop", &findStop},
};

const ow_module_desc_t kDesc{"webview", "0.1.0", kFns,
                             sizeof(kFns) / sizeof(kFns[0])};

} // namespace

namespace internal {
const ow_module_desc_t* WebviewModuleDescriptor() { return &kDesc; }
} // namespace internal

} // namespace ow
