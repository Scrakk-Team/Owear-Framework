// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/linux/Internal.hpp — helpers internos del backend WebKitGTK.
#pragma once
#include "../IWebviewBackend.hpp"
#include "../../Bridge/Dispatcher.hpp"
#include "../../Bridge/Shm.hpp"
#include "../../Control/ControlServer.hpp"
#include "../../Protocol/ProtocolRegistry.hpp"
#include "../../Session/PermissionBroker.hpp"
#include "../../Session/WebRequestBroker.hpp"
#include "../../Session/WindowOpenBroker.hpp"
#include "ow/Bridge/Codec.h"
#include "../../Core/Log.hpp"
#include "ow/Base64.h"
#include "ow/detail/minjson.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <cairo-pdf.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ow {
namespace webkitgtk_detail {

/// Trabajo de RPC diferido al pool (respuesta entregada en el hilo de UI).
struct RpcJob {
    WebKitURISchemeRequest* request = nullptr;
    WindowId wid = 0;
    std::string mod, fn, args;
    std::string out;
};

const char* MimeFromExt(const std::string& ext);
std::string ContentTypeFromHeaders(const std::string& headersJson);

cairo_status_t WritePngToStdString(void* closure, const unsigned char* data,
                                   unsigned int length);
cairo_status_t WriteToStdString(void* closure, const unsigned char* data,
                                unsigned int length);

gboolean OnDecidePolicy(WebKitWebView*, WebKitPolicyDecision* decision,
                        WebKitPolicyDecisionType type, gpointer);
gboolean OnPermissionRequest(WebKitWebView*, WebKitPermissionRequest* req, gpointer);

void FinishBytesStatic(WebKitURISchemeRequest* request, const uint8_t* data,
                       size_t len, const char* mime,
                       const char* extraHeaderName = nullptr,
                       const char* extraHeaderValue = nullptr);
void FinishBytesCopy(WebKitURISchemeRequest* request, const uint8_t* data,
                     size_t len, const char* mime,
                     const char* extraHeaderName = nullptr,
                     const char* extraHeaderValue = nullptr);
void FinishError(WebKitURISchemeRequest* request, int code, const char* msg);

std::string ExecRpc(WindowId wid, const std::string& mod, const std::string& fn,
                    const std::string& args);
bool RpcPoolEnabled();
bool RpcPoolModule(const std::string& m);
gboolean RpcJobFinish(gpointer data);
void RpcJobRun(gpointer data, gpointer);
GThreadPool* RpcPool();

std::string WebviewDataDir(const std::string& partition, const char* sub);
WebKitHardwareAccelerationPolicy GpuPolicy();
std::string PartitionFromArgs(const std::vector<std::string>& args);

} // namespace webkitgtk_detail
} // namespace ow
