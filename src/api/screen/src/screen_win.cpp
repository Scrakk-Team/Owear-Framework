// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/screen/src/screen_win.cpp — monitores (Win32).
//
// `id` ESTABLE = hash FNV-1a de `MONITORINFOEX.szDevice` (`\\.\DISPLAYn`), la
// misma idea que Electron ("un hash generado del device"). Los eventos se
// detectan con una ventana oculta de nivel superior en un hilo propio que
// recibe WM_DISPLAYCHANGE (los broadcast NO llegan a HWND_MESSAGE).
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace scr {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

namespace {

const ow_module_host_t* g_host = nullptr;

struct Mon {
    int64_t id = 0;
    bool primary = false;
    std::string label;
    RECT bounds{};
    RECT work{};
    int scale = 1;
    int rotation = 0;
    int colorDepth = 0;
    int frequency = 0;
    bool internal = false;
};

int64_t IdForDevice(const std::string& dev) {
    uint32_t h = 2166136261u; // FNV-1a 32 (cabe exacto en un number JS)
    for (unsigned char c : dev) {
        h ^= c;
        h *= 16777619u;
    }
    if (h == 0) h = 1;
    return static_cast<int64_t>(h);
}

int RotationDegrees(const DEVMODEA& dm) {
    switch (dm.dmDisplayOrientation) {
        case DMDO_90: return 90;
        case DMDO_180: return 180;
        case DMDO_270: return 270;
        default: return 0;
    }
}

int ScaleForMonitor(HMONITOR h) {
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static GetDpiForMonitorFn fn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore)
            fn = reinterpret_cast<GetDpiForMonitorFn>(
                GetProcAddress(shcore, "GetDpiForMonitor"));
    }
    if (!fn) return 1;
    UINT dx = 96, dy = 96;
    // MDT_EFFECTIVE_DPI = 0
    if (SUCCEEDED(fn(h, 0, &dx, &dy)) && dx > 0) {
        const int s = static_cast<int>((dx + 48) / 96);
        return s > 0 ? s : 1;
    }
    return 1;
}

// szDevice ("\\.\DISPLAY1") → {label, internal} vía QueryDisplayConfig.
std::map<std::string, std::pair<std::string, bool>> QueryTargets() {
    std::map<std::string, std::pair<std::string, bool>> out;
    UINT32 nPath = 0, nMode = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPath, &nMode) != ERROR_SUCCESS)
        return out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPath);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nMode);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPath, paths.data(), &nMode,
                           modes.data(), nullptr) != ERROR_SUCCESS)
        return out;
    for (UINT32 i = 0; i < nPath; ++i) {
        // El nombre GDI ("\\.\DISPLAYn") viene del SOURCE, no del target.
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sn{};
        sn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sn.header.size = sizeof(sn);
        sn.header.adapterId = paths[i].sourceInfo.adapterId;
        sn.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sn.header) != ERROR_SUCCESS) continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME tn{};
        tn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tn.header.size = sizeof(tn);
        tn.header.adapterId = paths[i].targetInfo.adapterId;
        tn.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tn.header) != ERROR_SUCCESS) continue;
        char gdi[64] = {0};
        WideCharToMultiByte(CP_UTF8, 0, sn.viewGdiDeviceName, -1, gdi,
                            sizeof(gdi) - 1, nullptr, nullptr);
        char label[128] = {0};
        WideCharToMultiByte(CP_UTF8, 0, tn.monitorFriendlyDeviceName, -1, label,
                            sizeof(label) - 1, nullptr, nullptr);
        const bool internal =
            tn.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
            tn.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS ||
            tn.outputTechnology ==
                DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
            tn.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
        out[gdi] = {label, internal};
    }
    return out;
}

BOOL CALLBACK MonProc(HMONITOR h, HDC, LPRECT, LPARAM data) {
    auto* out = reinterpret_cast<std::vector<Mon>*>(data);
    MONITORINFOEXA mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(h, &mi)) return TRUE;
    Mon m;
    m.id = IdForDevice(mi.szDevice);
    m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    m.label = mi.szDevice;
    m.bounds = mi.rcMonitor;
    m.work = mi.rcWork;
    m.scale = ScaleForMonitor(h);
    DEVMODEA dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
        m.rotation = RotationDegrees(dm);
        m.colorDepth = static_cast<int>(dm.dmBitsPerPel);
        m.frequency = static_cast<int>(dm.dmDisplayFrequency);
    }
    out->push_back(std::move(m));
    return TRUE;
}

std::vector<Mon> Enumerate() {
    std::vector<Mon> out;
    EnumDisplayMonitors(nullptr, nullptr, MonProc, reinterpret_cast<LPARAM>(&out));
    for (auto& t : QueryTargets())
        for (auto& m : out)
            if (m.label == t.first) {
                if (!t.second.first.empty()) m.label = t.second.first;
                m.internal = t.second.second;
            }
    return out;
}

std::map<int64_t, Mon> ById(std::vector<Mon> v) {
    std::map<int64_t, Mon> m;
    for (auto& x : v) m[x.id] = std::move(x);
    return m;
}

Object Rect(const RECT& r) {
    Object o;
    o.emplace_back("x", Value(static_cast<int64_t>(r.left)));
    o.emplace_back("y", Value(static_cast<int64_t>(r.top)));
    o.emplace_back("width", Value(static_cast<int64_t>(r.right - r.left)));
    o.emplace_back("height", Value(static_cast<int64_t>(r.bottom - r.top)));
    return o;
}

std::string Fingerprint(const Mon& m) {
    return std::to_string(m.bounds.left) + "," + std::to_string(m.bounds.top) + "," +
           std::to_string(m.bounds.right) + "," + std::to_string(m.bounds.bottom) + "/" +
           std::to_string(m.work.left) + "," + std::to_string(m.work.top) + "," +
           std::to_string(m.work.right) + "," + std::to_string(m.work.bottom) + "/" +
           std::to_string(m.scale) + "/" + std::to_string(m.rotation);
}

Object DisplayToJson(const Mon& m) {
    Object o;
    o.emplace_back("id", Value(m.id));
    o.emplace_back("primary", Value(m.primary));
    o.emplace_back("label", Value(m.label));
    o.emplace_back("bounds", Value(Rect(m.bounds)));
    {
        Object s;
        s.emplace_back("width", Value(static_cast<int64_t>(m.bounds.right - m.bounds.left)));
        s.emplace_back("height", Value(static_cast<int64_t>(m.bounds.bottom - m.bounds.top)));
        o.emplace_back("size", Value(std::move(s)));
    }
    o.emplace_back("workArea", Value(Rect(m.work)));
    {
        Object s;
        s.emplace_back("width", Value(static_cast<int64_t>(m.work.right - m.work.left)));
        s.emplace_back("height", Value(static_cast<int64_t>(m.work.bottom - m.work.top)));
        o.emplace_back("workAreaSize", Value(std::move(s)));
    }
    o.emplace_back("scaleFactor", Value(static_cast<int64_t>(m.scale)));
    o.emplace_back("rotation", Value(static_cast<int64_t>(m.rotation)));
    o.emplace_back("internal", Value(m.internal));
    o.emplace_back("detected", Value(true));
    o.emplace_back("displayFrequency", Value(static_cast<int64_t>(m.frequency)));
    o.emplace_back("colorDepth", Value(static_cast<int64_t>(m.colorDepth)));
    o.emplace_back("depthPerComponent", Value(static_cast<int64_t>(8)));
    o.emplace_back("colorSpace", Value(std::string("srgb")));
    o.emplace_back("monochrome", Value(false));
    o.emplace_back("touchSupport", Value(std::string("unknown")));
    o.emplace_back("accelerometerSupport", Value(std::string("unknown")));
    {
        Object origin;
        origin.emplace_back("x", Value(static_cast<int64_t>(m.bounds.left)));
        origin.emplace_back("y", Value(static_cast<int64_t>(m.bounds.top)));
        o.emplace_back("nativeOrigin", Value(std::move(origin)));
    }
    return o;
}

void Emit(const char* name, const Object& payload) {
    if (g_host && g_host->emit_event)
        g_host->emit_event(g_host->ctx, 0, name, Value(payload).Serialize().c_str());
}

void EmitDisplay(const char* name, const Mon& m) {
    Object p;
    p.emplace_back("display", Value(DisplayToJson(m)));
    Emit(name, p);
}

// ── watcher (hilo + ventana oculta) ─────────────────────────────────────────
std::map<int64_t, Mon> g_known;
std::thread g_thread;
std::atomic<bool> g_running{false};
HWND g_hwnd = nullptr;

void OnDisplayChange() {
    auto now = ById(Enumerate());
    for (const auto& [id, old] : g_known)
        if (!now.count(id)) EmitDisplay("screen.removed", old);
    for (const auto& [id, m] : now) {
        auto it = g_known.find(id);
        if (it == g_known.end()) {
            EmitDisplay("screen.added", m);
            continue;
        }
        if (Fingerprint(it->second) == Fingerprint(m)) continue;
        Array metrics;
        if (it->second.bounds.left != m.bounds.left ||
            it->second.bounds.top != m.bounds.top ||
            it->second.bounds.right != m.bounds.right ||
            it->second.bounds.bottom != m.bounds.bottom)
            metrics.emplace_back(Value(std::string("bounds")));
        if (it->second.work.left != m.work.left || it->second.work.top != m.work.top ||
            it->second.work.right != m.work.right ||
            it->second.work.bottom != m.work.bottom)
            metrics.emplace_back(Value(std::string("workArea")));
        if (it->second.scale != m.scale)
            metrics.emplace_back(Value(std::string("scaleFactor")));
        if (it->second.rotation != m.rotation)
            metrics.emplace_back(Value(std::string("rotation")));
        if (metrics.empty()) metrics.emplace_back(Value(std::string("bounds")));
        Object p;
        p.emplace_back("display", Value(DisplayToJson(m)));
        p.emplace_back("metrics", Value(std::move(metrics)));
        Emit("screen.changed", p);
    }
    g_known = std::move(now);
}

LRESULT CALLBACK WatchProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_DISPLAYCHANGE) {
        OnDisplayChange();
        return 0;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(h);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

void WatchThread() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WatchProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"owear-screen-watch";
    RegisterClassExW(&wc);
    // Ventana oculta de NIVEL SUPERIOR (no HWND_MESSAGE): los mensajes
    // broadcast como WM_DISPLAYCHANGE no llegan a las message-only windows.
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"owear-screen", 0, 0, 0, 0, 0,
                             nullptr, nullptr, wc.hInstance, nullptr);
    g_known = ById(Enumerate()); // semilla: no emite eventos
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_hwnd = nullptr;
}

} // namespace

void getAllDisplays(const ow_request_t*, ow_response_t* res) {
    Array arr;
    for (const auto& m : Enumerate())
        arr.emplace_back(Value(DisplayToJson(m)));
    RespondOk(res, Value(std::move(arr)).Serialize().c_str());
}

void getPrimaryDisplay(const ow_request_t*, ow_response_t* res) {
    for (const auto& m : Enumerate())
        if (m.primary) return RespondOk(res, Value(DisplayToJson(m)).Serialize().c_str());
    auto all = Enumerate();
    if (all.empty()) return RespondOk(res, "null");
    RespondOk(res, Value(DisplayToJson(all[0])).Serialize().c_str());
}

void getCursorScreenPoint(const ow_request_t*, ow_response_t* res) {
    POINT p{0, 0};
    GetCursorPos(&p);
    Object o;
    o.emplace_back("x", Value(static_cast<int64_t>(p.x)));
    o.emplace_back("y", Value(static_cast<int64_t>(p.y)));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

void watch(const ow_request_t*, ow_response_t* res) {
    if (g_running.load()) return RespondOk(res, "null");
    g_running = true;
    g_thread = std::thread(WatchThread);
    RespondOk(res, "null");
}

void unwatch(const ow_request_t*, ow_response_t* res) {
    if (!g_running.load()) return RespondOk(res, "null");
    g_running = false;
    if (g_hwnd) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    if (g_thread.joinable()) g_thread.join();
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
    static const ow_module_desc_t d{"screen", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
