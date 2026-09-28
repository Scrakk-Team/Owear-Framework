// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/theme/src/theme_linux.cpp — nativeTheme en Linux (GSettings + GTK).
//
// Existe una sola preferencia del sistema; el "source" (system|light|dark) solo
// la FUERZA sobre la ventana del proceso. `watch` escucha GSettings
// (color-scheme) y el tema GTK, y emite `theme.changed`.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gtk/gtk.h>

#include <string>

namespace th {

using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;
static std::string g_source = "system";
static GSettings* g_iface = nullptr;
static gulong g_sigIface = 0;
static gulong g_sigGtk = 0;

/// Lee la preferencia efectiva (respetando el override de `setSource`).
static bool ReadDark() {
    if (g_source == "dark") return true;
    if (g_source == "light") return false;

    GtkSettings* gs = gtk_settings_get_default();
    if (gs) {
        gboolean preferDark = FALSE;
        g_object_get(gs, "gtk-application-prefer-dark-theme", &preferDark, nullptr);
        if (preferDark) return true;
    }

    // GNOME 42+: org.gnome.desktop.interface color-scheme = prefer-dark | default.
    GSettingsSchemaSource* src = g_settings_schema_source_get_default();
    if (src) {
        GSettingsSchema* schema =
            g_settings_schema_source_lookup(src, "org.gnome.desktop.interface", TRUE);
        if (schema) {
            if (g_settings_schema_has_key(schema, "color-scheme")) {
                GSettings* s = g_settings_new("org.gnome.desktop.interface");
                gchar* cs = g_settings_get_string(s, "color-scheme");
                const bool dark = cs && g_strcmp0(cs, "prefer-dark") == 0;
                g_free(cs);
                g_object_unref(s);
                g_settings_schema_unref(schema);
                return dark;
            }
            g_settings_schema_unref(schema);
        }
    }

    // Fallback: nombre del tema GTK.
    if (gs) {
        gchar* name = nullptr;
        g_object_get(gs, "gtk-theme-name", &name, nullptr);
        const bool dark = name && (g_strrstr(name, "-dark") || g_strrstr(name, "Dark"));
        g_free(name);
        return dark;
    }
    return false;
}

static std::string Payload() {
    std::string s = "{\"dark\":";
    s += ReadDark() ? "true" : "false";
    s += ",\"source\":\"";
    s += g_source;
    s += "\",\"highContrast\":false,\"reducedTransparency\":false}";
    return s;
}

static void EmitChanged() {
    if (g_host && g_host->emit_event)
        g_host->emit_event(g_host->ctx, 0, "theme.changed", Payload().c_str());
}

static void OnIfaceChanged(GSettings*, gchar*, gpointer) { EmitChanged(); }
static void OnGtkChanged(GObject*, GParamSpec*, gpointer) { EmitChanged(); }

void get(const ow_request_t*, ow_response_t* res) { RespondOk(res, Payload().c_str()); }

void isDark(const ow_request_t*, ow_response_t* res) {
    RespondOk(res, ReadDark() ? "true" : "false");
}

void setSource(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    std::string src = "system";
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString())
        src = parsed.value->AsArray()[0].AsString();
    if (src != "system" && src != "light" && src != "dark")
        return RespondError(res, "source inválido (system|light|dark)");
    g_source = src;
    if (GtkSettings* gs = gtk_settings_get_default()) {
        g_object_set(gs, "gtk-application-prefer-dark-theme",
                     static_cast<gboolean>(src == "dark"), nullptr);
    }
    RespondOk(res, Payload().c_str());
    EmitChanged();
}

void watch(const ow_request_t*, ow_response_t* res) {
    if (g_sigIface || g_sigGtk) return RespondOk(res, "null");
    GSettingsSchemaSource* src = g_settings_schema_source_get_default();
    if (src) {
        GSettingsSchema* schema =
            g_settings_schema_source_lookup(src, "org.gnome.desktop.interface", TRUE);
        if (schema) {
            if (g_settings_schema_has_key(schema, "color-scheme")) {
                g_iface = g_settings_new("org.gnome.desktop.interface");
                g_sigIface = g_signal_connect(g_iface, "changed::color-scheme",
                                              G_CALLBACK(OnIfaceChanged), nullptr);
            }
            g_settings_schema_unref(schema);
        }
    }
    if (GtkSettings* gs = gtk_settings_get_default())
        g_sigGtk = g_signal_connect(gs, "notify::gtk-theme-name",
                                    G_CALLBACK(OnGtkChanged), nullptr);
    RespondOk(res, "null");
}

void unwatch(const ow_request_t*, ow_response_t* res) {
    if (g_iface && g_sigIface) g_signal_handler_disconnect(g_iface, g_sigIface);
    if (g_iface) g_object_unref(g_iface);
    g_iface = nullptr;
    g_sigIface = 0;
    if (GtkSettings* gs = gtk_settings_get_default())
        if (gs && g_sigGtk) g_signal_handler_disconnect(gs, g_sigGtk);
    g_sigGtk = 0;
    RespondOk(res, "null");
}

} // namespace th

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    th::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"get", &th::get},         {"isDark", &th::isDark},
        {"setSource", &th::setSource}, {"watch", &th::watch},
        {"unwatch", &th::unwatch},
    };
    static const ow_module_desc_t d{"theme", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
