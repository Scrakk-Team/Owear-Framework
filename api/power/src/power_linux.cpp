// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/power/src/power_linux.cpp — energía en Linux.
//
//  monitor   logind (PrepareForSleep/PrepareForShutdown + Session Lock/Unlock)
//  batería   UPower (OnBattery + PropertiesChanged), fallback /sys
//  idle      X11 MIT-SCREEN-SAVER (libXss, dlopen opcional); Wayland → unknown
//  inhibir   org.freedesktop.ScreenSaver (Inhibit/UnInhibit)
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gio/gio.h>

#include <X11/Xlib.h> // XScreenSaverQueryInfo (solo tipos Xlib; Xss se dlopen)

#include <dlfcn.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace pw {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

namespace {

const ow_module_host_t* g_host = nullptr;

GDBusConnection* g_sysBus = nullptr;
bool g_watching = false;
bool g_locked = false;
bool g_onBattery = false;
bool g_haveBattery = false;
std::vector<guint> g_subs;

// ── inhibidores ─────────────────────────────────────────────────────────────
int g_nextInhibit = 1;
std::map<int, guint32> g_inhibits;

void LogWarn(const std::string& m) {
    if (g_host && g_host->log) g_host->log(g_host->ctx, 2, m.c_str());
}

void Emit(const char* name) {
    if (g_host && g_host->emit_event)
        g_host->emit_event(g_host->ctx, 0, name, "null");
}

// ── batería (UPower + sysfs) ────────────────────────────────────────────────
bool SysfsOnBattery() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path root = "/sys/class/power_supply";
    if (!fs::exists(root, ec)) return false;
    bool sawAc = false, acOnline = false, sawBat = false;
    for (auto& e : fs::directory_iterator(root, ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("AC", 0) == 0 || name.rfind("ADP", 0) == 0) {
            std::ifstream f(e.path() / "online");
            int v = -1;
            if (f >> v) {
                sawAc = true;
                acOnline = acOnline || v == 1;
            }
        } else if (name.rfind("BAT", 0) == 0) {
            sawBat = true;
        }
    }
    if (sawAc) return !acOnline;
    return sawBat ? g_onBattery : false; // sin datos de AC: conserva lo último
}

bool ReadOnBattery() {
    if (g_sysBus) {
        GError* e = nullptr;
        GVariant* r = g_dbus_connection_call_sync(
            g_sysBus, "org.freedesktop.UPower", "/org/freedesktop/UPower",
            "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", "org.freedesktop.UPower", "OnBattery"),
            G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &e);
        if (!e && r) {
            GVariant* inner = nullptr;
            g_variant_get(r, "(v)", &inner);
            bool on = false;
            if (inner) {
                on = g_variant_get_boolean(inner);
                g_variant_unref(inner);
            }
            g_variant_unref(r);
            g_haveBattery = true;
            return on;
        }
        if (e) g_error_free(e);
    }
    bool on = SysfsOnBattery();
    g_haveBattery = true;
    return on;
}

// ── señales D-Bus ───────────────────────────────────────────────────────────
void OnPrepareForSleep(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                       const gchar*, GVariant* params, gpointer) {
    gboolean sleeping = FALSE;
    g_variant_get(params, "(b)", &sleeping);
    Emit(sleeping ? "power.suspend" : "power.resume");
}

void OnPrepareForShutdown(GDBusConnection*, const gchar*, const gchar*,
                          const gchar*, const gchar*, GVariant* params, gpointer) {
    gboolean down = FALSE;
    g_variant_get(params, "(b)", &down);
    if (down) Emit("power.shutdown");
}

void OnLock(GDBusConnection*, const gchar*, const gchar*, const gchar*,
            const gchar*, GVariant*, gpointer) {
    g_locked = true;
    Emit("power.lock");
}

void OnUnlock(GDBusConnection*, const gchar*, const gchar*, const gchar*,
              const gchar*, GVariant*, gpointer) {
    g_locked = false;
    Emit("power.unlock");
}

void OnPropertiesChanged(GDBusConnection*, const gchar*, const gchar*,
                         const gchar*, const gchar*, GVariant* params, gpointer) {
    const gchar* iface = nullptr;
    GVariant* changed = nullptr;
    GVariant* invalidated = nullptr;
    g_variant_get(params, "(&s@a{sv}@as)", &iface, &changed, &invalidated);
    if (iface && std::string(iface) == "org.freedesktop.UPower" && changed) {
        GVariant* v = g_variant_lookup_value(changed, "OnBattery",
                                             G_VARIANT_TYPE_BOOLEAN);
        if (v) {
            const bool on = g_variant_get_boolean(v);
            g_variant_unref(v);
            if (on != g_onBattery) {
                g_onBattery = on;
                Emit(on ? "power.battery" : "power.ac");
            }
        }
    }
    if (changed) g_variant_unref(changed);
    if (invalidated) g_variant_unref(invalidated);
}

void Subscribe(const char* sender, const char* iface, const char* member,
               const char* path, GDBusSignalCallback cb) {
    if (!g_sysBus) return;
    g_subs.push_back(g_dbus_connection_signal_subscribe(
        g_sysBus, sender, iface, member, path, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, cb, nullptr, nullptr));
}

// ── idle (X11 XScreenSaver, dlopen) ─────────────────────────────────────────
struct XScreenSaverInfo {
    unsigned long window;
    int state;
    int kind;
    unsigned long til_or_since;
    unsigned long idle;
    unsigned long eventMask;
};
using AllocInfoFn = XScreenSaverInfo* (*)();
using QueryInfoFn = int (*)(Display*, Drawable, XScreenSaverInfo*);

unsigned long XssIdleMs(bool& available) {
    static bool tried = false;
    static AllocInfoFn alloc = nullptr;
    static QueryInfoFn query = nullptr;
    static Display* dpy = nullptr;
    if (!tried) {
        tried = true;
        void* h = dlopen("libXss.so.1", RTLD_LAZY);
        if (h) {
            alloc = reinterpret_cast<AllocInfoFn>(dlsym(h, "XScreenSaverAllocInfo"));
            query = reinterpret_cast<QueryInfoFn>(dlsym(h, "XScreenSaverQueryInfo"));
        }
        dpy = XOpenDisplay(nullptr);
    }
    available = alloc && query && dpy;
    if (!available) return 0;
    XScreenSaverInfo* info = alloc();
    if (!info) {
        available = false;
        return 0;
    }
    unsigned long idle = 0;
    if (query(dpy, DefaultRootWindow(dpy), info)) idle = info->idle;
    XFree(info);
    return idle;
}

} // namespace

// ── monitor ─────────────────────────────────────────────────────────────────
void monitorStart(const ow_request_t*, ow_response_t* res) {
    if (g_watching) return RespondOk(res, "\"ya-activo\"");
    g_sysBus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    if (!g_sysBus) {
        LogWarn("power: sin bus de sistema (¿contenedor?); sin eventos");
        return RespondOk(res, "null");
    }
    Subscribe("org.freedesktop.login1", "org.freedesktop.login1.Manager",
              "PrepareForSleep", "/org/freedesktop/login1", &OnPrepareForSleep);
    Subscribe("org.freedesktop.login1", "org.freedesktop.login1.Manager",
              "PrepareForShutdown", "/org/freedesktop/login1", &OnPrepareForShutdown);
    Subscribe("org.freedesktop.login1", "org.freedesktop.login1.Session", "Lock",
              "/org/freedesktop/login1/session/auto", &OnLock);
    Subscribe("org.freedesktop.login1", "org.freedesktop.login1.Session", "Unlock",
              "/org/freedesktop/login1/session/auto", &OnUnlock);
    Subscribe("org.freedesktop.UPower", "org.freedesktop.DBus.Properties",
              "PropertiesChanged", "/org/freedesktop/UPower", &OnPropertiesChanged);
    g_onBattery = ReadOnBattery();
    g_watching = true;
    RespondOk(res, "null");
}

void monitorStop(const ow_request_t*, ow_response_t* res) {
    if (g_sysBus) {
        for (guint id : g_subs) g_dbus_connection_signal_unsubscribe(g_sysBus, id);
        g_object_unref(g_sysBus);
        g_sysBus = nullptr;
    }
    g_subs.clear();
    g_watching = false;
    RespondOk(res, "null");
}

// ── idle / batería ──────────────────────────────────────────────────────────
void idleTime(const ow_request_t*, ow_response_t* res) {
    bool available = false;
    const unsigned long ms = XssIdleMs(available);
    RespondOk(res, available ? std::to_string(ms / 1000).c_str() : "0");
}

void idleState(const ow_request_t* req, ow_response_t* res) {
    int threshold = 0;
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsNumber())
        threshold = static_cast<int>(parsed.value->AsArray()[0].AsInt());
    if (g_locked) return RespondOk(res, "\"locked\"");
    bool available = false;
    const unsigned long ms = XssIdleMs(available);
    if (!available) return RespondOk(res, "\"unknown\"");
    const bool idle = static_cast<int>(ms / 1000) >= threshold;
    RespondOk(res, idle ? "\"idle\"" : "\"active\"");
}

void isOnBattery(const ow_request_t*, ow_response_t* res) {
    RespondOk(res, ReadOnBattery() ? "true" : "false");
}

// ── inhibidores ─────────────────────────────────────────────────────────────
void inhibitStart(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    std::string what = "prevent-display-sleep";
    if (parsed.value && parsed.value->IsArray() &&
        !parsed.value->AsArray().empty() && parsed.value->AsArray()[0].IsString())
        what = parsed.value->AsArray()[0].AsString();
    (void)what; // v1: siempre display-sleep via ScreenSaver

    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return RespondError(res, "sin sesión D-Bus");
    GError* e = nullptr;
    GVariant* r = g_dbus_connection_call_sync(
        bus, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
        "org.freedesktop.ScreenSaver", "Inhibit",
        g_variant_new("(ss)", "owear-app", "owear blocker"),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &e);
    g_object_unref(bus);
    if (e) {
        std::string err = e->message;
        g_error_free(e);
        return RespondError(res, err);
    }
    guint32 cookie = 0;
    g_variant_get(r, "(u)", &cookie);
    g_variant_unref(r);
    const int id = g_nextInhibit++;
    g_inhibits[id] = cookie;
    RespondOk(res, Value(static_cast<int64_t>(id)).Serialize().c_str());
}

void inhibitStop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "inhibitId requerido");
    const int id = static_cast<int>(parsed.value->AsArray()[0].AsInt());
    auto it = g_inhibits.find(id);
    if (it == g_inhibits.end()) return RespondOk(res, "false");
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    bool ok = false;
    if (bus) {
        GError* e = nullptr;
        g_dbus_connection_call_sync(bus, "org.freedesktop.ScreenSaver",
                                    "/org/freedesktop/ScreenSaver",
                                    "org.freedesktop.ScreenSaver", "UnInhibit",
                                    g_variant_new("(u)", it->second), nullptr,
                                    G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &e);
        if (!e) ok = true;
        else g_error_free(e);
        g_object_unref(bus);
    }
    g_inhibits.erase(it);
    RespondOk(res, ok ? "true" : "false");
}

} // namespace pw

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    pw::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"monitorStart", &pw::monitorStart}, {"monitorStop", &pw::monitorStop},
        {"idleTime", &pw::idleTime},         {"idleState", &pw::idleState},
        {"isOnBattery", &pw::isOnBattery},   {"inhibitStart", &pw::inhibitStart},
        {"inhibitStop", &pw::inhibitStop},
    };
    static const ow_module_desc_t d{
        "power", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
