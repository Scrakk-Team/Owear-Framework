// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/linux/Helpers.cpp — helpers del backend WebKitGTK (RPC pool, esquemas, permisos).
#include "Internal.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ow {
namespace webkitgtk_detail {

cairo_status_t WritePngToStdString(void* closure, const unsigned char* data,
                                   unsigned int length) {
    static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data),
                                               length);
    return CAIRO_STATUS_SUCCESS;
}

/// Escribe cualquier salida de cairo (p. ej. PDF) a un std::string.
cairo_status_t WriteToStdString(void* closure, const unsigned char* data,
                                unsigned int length) {
    static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data),
                                               length);
    return CAIRO_STATUS_SUCCESS;
}

/// webRequest (Linux): intercepta NAVEGACIONES (decide-policy) + window.open.
gboolean OnDecidePolicy(WebKitWebView*, WebKitPolicyDecision* decision,
                        WebKitPolicyDecisionType type, gpointer) {
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
        type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION)
        return FALSE;
    WebKitNavigationAction* action =
        webkit_navigation_policy_decision_get_navigation_action(
            WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    if (!action) return FALSE;
    WebKitURIRequest* req = webkit_navigation_action_get_request(action);
    const char* url = req ? webkit_uri_request_get_uri(req) : nullptr;

    // window.open / target=_blank → la app decide (setWindowOpenHandler).
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION &&
        ow::WindowOpenBroker::Get().Enabled()) {
        g_object_ref(decision);
        ow::WindowOpenBroker::Get().Request(
            std::string(url ? url : ""), [decision](bool allow) {
                if (allow)
                    webkit_policy_decision_use(decision);
                else
                    webkit_policy_decision_ignore(decision);
                g_object_unref(decision);
            });
        return TRUE;
    }

    if (!ow::WebRequestBroker::Get().Enabled()) return FALSE;
    if (!url || !ow::WebRequestBroker::Get().Matches(url)) return FALSE;

    g_object_ref(decision);
    ow::WebRequestBroker::Get().BeforeRequest(
        std::string(url), "GET", "{}", [decision](ow::WebRequestBroker::Action a) {
            if (a.cancel) webkit_policy_decision_ignore(decision);
            else webkit_policy_decision_use(decision);
            g_object_unref(decision);
        });
    return TRUE; // gestionado (deferido)
}

/// Permisos del WebView: delega en PermissionBroker (la app decide).
gboolean OnPermissionRequest(WebKitWebView*, WebKitPermissionRequest* req, gpointer) {    const char* name = "unknown";
    if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(req)) name = "geolocation";
    else if (WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(req)) name = "notifications";
    else if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(req)) name = "media";
    else if (WEBKIT_IS_POINTER_LOCK_PERMISSION_REQUEST(req)) name = "pointerLock";
    g_object_ref(req);
    ow::PermissionBroker::Get().Request(name, "", [req](bool allow) {
        if (allow) webkit_permission_request_allow(req);
        else webkit_permission_request_deny(req);
        g_object_unref(req);
    });
    return TRUE;
}

// Directorio de datos del WebView AISLADO POR APP (data/cache). Antes se usaba
// el WebsiteDataManager por defecto (compartido) → localStorage/IndexedDB/cache
// se cruzaban entre apps Owear. Se elige por OW_APP_ID (o OW_APP_NAME).
// `partition` añade aislamiento por perfil (session API).
std::string WebviewDataDir(const std::string& partition, const char* sub) {
    std::string base;
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        base = xdg;
    else if (const char* home = std::getenv("HOME"); home && *home)
        base = std::string(home) + "/.local/share";
    else
        base = "/tmp";
    std::string app = "owear";
    if (const char* id = std::getenv("OW_APP_ID"); id && *id)
        app = id;
    else if (const char* name = std::getenv("OW_APP_NAME"); name && *name)
        app = name;
    // Sanea la partición para que sea un nombre de carpeta válido.
    std::string part = partition.empty() ? "default" : partition;
    for (char& c : part)
        if (c == ':' || c == '/' || c == '\\' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            c = '_';
    return base + "/owear/" + app + "/webkit/" + part + "/" + sub;
}

/// Política de aceleración de hardware (OW_GPU=auto|on|off).
/// `auto` (default) = ON_DEMAND (comportamiento histórico). `off` = NEVER:
/// el renderer DMABUF cuesta ~550 ms + ~30 MB al arrancar en entornos sin GPU
/// (headless/Xvfb); las apps sin GPU o de CI deberían usar `off`.
WebKitHardwareAccelerationPolicy GpuPolicy() {
    const char* v = std::getenv("OW_GPU");
    const std::string g = v ? v : "auto";
    if (g == "off") return WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER;
    if (g == "on") return WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS;
    return WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND;
}

/// Extrae `ow-partition=<nombre>` de los args del WebView.
std::string PartitionFromArgs(const std::vector<std::string>& args) {
    static const std::string kPrefix = "ow-partition=";
    for (const auto& a : args) {
        if (a.rfind(kPrefix, 0) == 0 && a.size() > kPrefix.size())
            return a.substr(kPrefix.size());
    }
    return {};
}


} // namespace webkitgtk_detail
} // namespace ow
