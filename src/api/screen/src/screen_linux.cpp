// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/screen/src/screen_linux.cpp — monitores (GDK).
//
// `id` es ESTABLE dentro de la sesión: se asigna al primer avistamiento de
// cada `GdkMonitor*` y se conserva hasta que el monitor desaparece. Así los
// eventos added/removed/changed pueden referirse al mismo display sin
// depender del índice de enumeración (que cambia al conectar/desconectar).
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gdk/gdk.h>

#include <cstdint>
#include <map>
#include <string>

namespace scr {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

namespace {

const ow_module_host_t* g_host = nullptr;

// GdkMonitor* → id estable. Se libera al recibir `monitor-removed`.
std::map<GdkMonitor*, int64_t> g_idByMon;
int64_t g_nextId = 1;

// Monitores con señales de métricas conectadas.
std::map<GdkMonitor*, gulong> g_metricGeo;
std::map<GdkMonitor*, gulong> g_metricWork;
std::map<GdkMonitor*, gulong> g_metricScale;

GdkDisplay* g_display = nullptr;
gulong g_sigAdded = 0;
gulong g_sigRemoved = 0;

int64_t IdFor(GdkMonitor* m) {
    auto it = g_idByMon.find(m);
    if (it != g_idByMon.end()) return it->second;
    const int64_t id = g_nextId++;
    g_idByMon[m] = id;
    return id;
}

void Emit(const char* name, const Value& payload) {
    if (g_host && g_host->emit_event)
        g_host->emit_event(g_host->ctx, 0, name, payload.Serialize().c_str());
}

Object Rect(const GdkRectangle& r) {
    Object o;
    o.emplace_back("x", Value(r.x));
    o.emplace_back("y", Value(r.y));
    o.emplace_back("width", Value(r.width));
    o.emplace_back("height", Value(r.height));
    return o;
}

/// Objeto Display completo (campos que GDK3 puede dar; el resto, defaults
/// honestos). Véase docs/APIS.md.
Object DisplayToJson(GdkDisplay* d, GdkMonitor* m) {
    GdkRectangle geo{}, work{};
    gdk_monitor_get_geometry(m, &geo);
    gdk_monitor_get_workarea(m, &work);
    const int scale = gdk_monitor_get_scale_factor(m);
    const int refreshMhz = gdk_monitor_get_refresh_rate(m); // mHz (0 = desconocido)
    const char* model = gdk_monitor_get_model(m);
    const bool primary = gdk_display_get_primary_monitor(d) == m;

    Object o;
    o.emplace_back("id", Value(IdFor(m)));
    o.emplace_back("primary", Value(primary));
    o.emplace_back("label", Value(std::string(model ? model : "")));
    o.emplace_back("bounds", Value(Rect(geo)));
    {
        Object s;
        s.emplace_back("width", Value(geo.width));
        s.emplace_back("height", Value(geo.height));
        o.emplace_back("size", Value(std::move(s)));
    }
    o.emplace_back("workArea", Value(Rect(work)));
    {
        Object s;
        s.emplace_back("width", Value(work.width));
        s.emplace_back("height", Value(work.height));
        o.emplace_back("workAreaSize", Value(std::move(s)));
    }
    o.emplace_back("scaleFactor", Value(static_cast<int64_t>(scale > 0 ? scale : 1)));
    o.emplace_back("rotation", Value(static_cast<int64_t>(0))); // GDK3 no lo expone
    o.emplace_back("internal", Value(false));                  // sin API fiable en GDK
    o.emplace_back("detected", Value(true));
    o.emplace_back("displayFrequency",
                   Value(static_cast<int64_t>(refreshMhz > 0 ? refreshMhz / 1000 : 0)));
    o.emplace_back("colorDepth", Value(static_cast<int64_t>(32)));
    o.emplace_back("depthPerComponent", Value(static_cast<int64_t>(8)));
    o.emplace_back("colorSpace", Value(std::string("srgb")));
    o.emplace_back("monochrome", Value(false));
    o.emplace_back("touchSupport", Value(std::string("unknown")));
    o.emplace_back("accelerometerSupport", Value(std::string("unknown")));
    {
        Object origin;
        origin.emplace_back("x", Value(geo.x));
        origin.emplace_back("y", Value(geo.y));
        o.emplace_back("nativeOrigin", Value(std::move(origin)));
    }
    return o;
}

void ConnectMetrics(GdkMonitor* m);

void OnMonitorAdded(GdkDisplay* d, GdkMonitor* m, gpointer) {
    ConnectMetrics(m);
    Object p;
    p.emplace_back("display", Value(DisplayToJson(d, m)));
    Emit("screen.added", Value(std::move(p)));
}

void OnMonitorRemoved(GdkDisplay* d, GdkMonitor* m, gpointer) {
    Object p;
    p.emplace_back("display", Value(DisplayToJson(d, m)));
    Emit("screen.removed", Value(std::move(p)));
    // El objeto se destruirá: libera su id para que una reutilización de
    // puntero reciba un id nuevo.
    g_idByMon.erase(m);
    g_metricGeo.erase(m);
    g_metricWork.erase(m);
    g_metricScale.erase(m);
}

/// `notify::geometry|workarea|scale-factor` de un monitor → screen.changed.
void OnMetric(GObject* obj, GParamSpec* pspec, gpointer) {
    GdkMonitor* m = GDK_MONITOR(obj);
    std::string metric = "bounds";
    if (pspec && pspec->name) {
        const std::string n = pspec->name;
        if (n == "workarea") metric = "workArea";
        else if (n == "scale-factor") metric = "scaleFactor";
        else if (n == "geometry") metric = "bounds";
        else metric = n;
    }
    Object p;
    p.emplace_back("display", Value(DisplayToJson(g_display, m)));
    {
        Array metrics;
        metrics.emplace_back(Value(metric));
        p.emplace_back("metrics", Value(std::move(metrics)));
    }
    Emit("screen.changed", Value(std::move(p)));
}

void ConnectMetrics(GdkMonitor* m) {
    if (g_metricGeo.count(m)) return;
    g_metricGeo[m] = g_signal_connect(m, "notify::geometry", G_CALLBACK(OnMetric), nullptr);
    g_metricWork[m] = g_signal_connect(m, "notify::workarea", G_CALLBACK(OnMetric), nullptr);
    g_metricScale[m] =
        g_signal_connect(m, "notify::scale-factor", G_CALLBACK(OnMetric), nullptr);
}

void ConnectAllMetrics() {
    if (!g_display) return;
    const int n = gdk_display_get_n_monitors(g_display);
    for (int i = 0; i < n; ++i) ConnectMetrics(gdk_display_get_monitor(g_display, i));
}

} // namespace

void getAllDisplays(const ow_request_t*, ow_response_t* res) {
    GdkDisplay* d = gdk_display_get_default();
    Array arr;
    const int n = gdk_display_get_n_monitors(d);
    for (int i = 0; i < n; ++i)
        arr.emplace_back(Value(DisplayToJson(d, gdk_display_get_monitor(d, i))));
    RespondOk(res, Value(std::move(arr)).Serialize().c_str());
}

void getPrimaryDisplay(const ow_request_t*, ow_response_t* res) {
    GdkDisplay* d = gdk_display_get_default();
    GdkMonitor* primary = gdk_display_get_primary_monitor(d);
    if (!primary) primary = gdk_display_get_monitor(d, 0);
    if (!primary) return RespondOk(res, "null");
    RespondOk(res, Value(DisplayToJson(d, primary)).Serialize().c_str());
}

void getCursorScreenPoint(const ow_request_t*, ow_response_t* res) {
    GdkDisplay* d = gdk_display_get_default();
    GdkSeat* seat = gdk_display_get_default_seat(d);
    GdkDevice* dev = seat ? gdk_seat_get_pointer(seat) : nullptr;
    int x = 0, y = 0;
    if (dev) gdk_device_get_position(dev, nullptr, &x, &y);
    Object o;
    o.emplace_back("x", Value(x));
    o.emplace_back("y", Value(y));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

void watch(const ow_request_t*, ow_response_t* res) {
    if (g_display) return RespondOk(res, "null");
    g_display = gdk_display_get_default();
    if (!g_display) return RespondError(res, "sin display GDK");
    g_sigAdded = g_signal_connect(g_display, "monitor-added",
                                  G_CALLBACK(OnMonitorAdded), nullptr);
    g_sigRemoved = g_signal_connect(g_display, "monitor-removed",
                                    G_CALLBACK(OnMonitorRemoved), nullptr);
    ConnectAllMetrics();
    RespondOk(res, "null");
}

void unwatch(const ow_request_t*, ow_response_t* res) {
    if (!g_display) return RespondOk(res, "null");
    if (g_sigAdded) g_signal_handler_disconnect(g_display, g_sigAdded);
    if (g_sigRemoved) g_signal_handler_disconnect(g_display, g_sigRemoved);
    g_sigAdded = g_sigRemoved = 0;
    for (auto& [m, id] : g_metricGeo)
        if (id) g_signal_handler_disconnect(m, id);
    for (auto& [m, id] : g_metricWork)
        if (id) g_signal_handler_disconnect(m, id);
    for (auto& [m, id] : g_metricScale)
        if (id) g_signal_handler_disconnect(m, id);
    g_metricGeo.clear();
    g_metricWork.clear();
    g_metricScale.clear();
    g_display = nullptr;
    RespondOk(res, "null");
}

} // namespace scr

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    scr::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"getAllDisplays", &scr::getAllDisplays},
        {"getPrimaryDisplay", &scr::getPrimaryDisplay},
        {"getCursorScreenPoint", &scr::getCursorScreenPoint},
        {"watch", &scr::watch},
        {"unwatch", &scr::unwatch},
    };
    static const ow_module_desc_t d{
        "screen", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
