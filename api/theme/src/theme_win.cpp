// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/theme/src/theme_win.cpp — nativeTheme en Windows (registro).
//
// AppsUseLightTheme (DWORD): 1 = claro, 0 = oscuro. `watch` sondea el registro
// en un hilo y emite `theme.changed` (el emit del host es thread-safe: encola en
// el bucle del kernel). Un `atexit` para/une el hilo al descargar el módulo.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

namespace th {

using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;
static std::string g_source = "system";
static std::atomic<bool> g_stop{true};
static std::thread g_thread;
static bool g_watching = false;
static bool g_atexit = false;

static bool ReadDark() {
    if (g_source == "dark") return true;
    if (g_source == "light") return false;
    DWORD value = 1;
    DWORD size = sizeof(value);
    LONG r = RegGetValueA(HKEY_CURRENT_USER,
                          "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                          "AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (r != ERROR_SUCCESS) return false;
    return value == 0; // 0 = oscuro
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

static void WatchLoop() {
    bool last = ReadDark();
    while (!g_stop.load()) {
        for (int i = 0; i < 20 && !g_stop.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (g_stop.load()) break;
        const bool now = ReadDark();
        if (now != last) {
            last = now;
            EmitChanged();
        }
    }
}

static void StopWatch() {
    g_stop = true;
    if (g_thread.joinable()) g_thread.join();
}

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
    RespondOk(res, Payload().c_str());
    EmitChanged();
}

void watch(const ow_request_t*, ow_response_t* res) {
    if (g_watching) return RespondOk(res, "null");
    g_watching = true;
    g_stop = false;
    g_thread = std::thread(WatchLoop);
    if (!g_atexit) {
        std::atexit(&StopWatch);
        g_atexit = true;
    }
    RespondOk(res, "null");
}

void unwatch(const ow_request_t*, ow_response_t* res) {
    g_stop = true;
    if (g_thread.joinable()) g_thread.join();
    g_watching = false;
    RespondOk(res, "null");
}

} // namespace th

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    th::g_host = h;
}

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
