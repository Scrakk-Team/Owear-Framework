// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/power/src/power_win.cpp — energía en Windows.
//
//  monitor   ventana oculta top-level + bucle de mensajes:
//              WM_POWERBROADCAST (suspend/resume/status) · WM_ENDSESSION (shutdown)
//              WM_WTSSESSION_CHANGE (lock/unlock, Wtsapi32 dinámico)
//  idle      GetLastInputInfo
//  batería   GetSystemPowerStatus
//  inhibir   SetThreadExecutionState
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#ifndef WM_WTSSESSION_CHANGE
#define WM_WTSSESSION_CHANGE 0x02B1
#endif
#ifndef WTS_SESSION_LOCK
#define WTS_SESSION_LOCK 0x7
#define WTS_SESSION_UNLOCK 0x8
#endif
#define OW_NOTIFY_FOR_THIS_SESSION 0

namespace pw {

using ow::json::Value;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

namespace {

const ow_module_host_t* g_host = nullptr;

void Emit(const char* name) {
    if (g_host && g_host->emit_event)
        g_host->emit_event(g_host->ctx, 0, name, "null");
}

// ── watcher (hilo + ventana oculta) ─────────────────────────────────────────
std::thread g_thread;
std::atomic<bool> g_running{false};
HWND g_hwnd = nullptr;
bool g_locked = false;
bool g_onBattery = false;
bool g_statusKnown = false;

using WtsRegisterFn = BOOL(WINAPI*)(HWND, DWORD);
using WtsUnregisterFn = BOOL(WINAPI*)(HWND);
WtsRegisterFn g_wtsReg = nullptr;
WtsUnregisterFn g_wtsUnreg = nullptr;

bool ReadOnBattery() {
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) return g_onBattery;
    g_statusKnown = true;
    if (sps.ACLineStatus == 0) return true;   // battery
    if (sps.ACLineStatus == 1) return false;  // AC
    return g_onBattery;                       // 255 = desconocido
}

void RefreshBattery() {
    const bool on = ReadOnBattery();
    if (!g_statusKnown || on == g_onBattery) return;
    g_onBattery = on;
    Emit(on ? "power.battery" : "power.ac");
}

LRESULT CALLBACK WatchProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_POWERBROADCAST:
            // PBT_APMSUSPEND=4 · PBT_APMRESUMESUSPEND=7 · PBT_APMRESUMEAUTOMATIC=18
            // PBT_APMPOWERSTATUSCHANGE=0xA
            if (w == 4) Emit("power.suspend");
            else if (w == 7 || w == 18) Emit("power.resume");
            else if (w == 0xA) RefreshBattery();
            return TRUE;
        case WM_ENDSESSION:
            if (w) Emit("power.shutdown");
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (w == WTS_SESSION_LOCK) {
                g_locked = true;
                Emit("power.lock");
            } else if (w == WTS_SESSION_UNLOCK) {
                g_locked = false;
                Emit("power.unlock");
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(h, msg, w, l);
    }
}

void WatchThread() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WatchProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"owear-power-watch";
    RegisterClassExW(&wc);
    // Ventana oculta top-level: WM_POWERBROADCAST/WM_ENDSESSION son broadcast
    // y no llegan a HWND_MESSAGE.
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"owear-power", WS_POPUP, 0, 0, 0,
                             0, nullptr, nullptr, wc.hInstance, nullptr);
    HMODULE wts = LoadLibraryW(L"wtsapi32.dll");
    if (wts && g_hwnd) {
        g_wtsReg = reinterpret_cast<WtsRegisterFn>(
            GetProcAddress(wts, "WTSRegisterSessionNotification"));
        g_wtsUnreg = reinterpret_cast<WtsUnregisterFn>(
            GetProcAddress(wts, "WTSUnRegisterSessionNotification"));
        if (g_wtsReg) g_wtsReg(g_hwnd, OW_NOTIFY_FOR_THIS_SESSION);
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_wtsUnreg && g_hwnd) g_wtsUnreg(g_hwnd);
    g_hwnd = nullptr;
}

// ── inhibidores ─────────────────────────────────────────────────────────────
std::mutex s_mu;
std::map<int, bool> s_inhibits; // id → preventAppSuspension
int s_nextInhibit = 1;

void ApplyUnion() {
    bool display = false, system = false;
    for (auto& [id, app] : s_inhibits) {
        if (app) system = true;
        else display = true;
    }
    ULONG flags = ES_CONTINUOUS;
    if (display) flags |= ES_DISPLAY_REQUIRED;
    if (system) flags |= ES_SYSTEM_REQUIRED;
    SetThreadExecutionState(flags);
}

} // namespace

// ── monitor ─────────────────────────────────────────────────────────────────
void monitorStart(const ow_request_t*, ow_response_t* res) {
    if (g_running.load()) return RespondOk(res, "\"ya-activo\"");
    g_onBattery = ReadOnBattery();
    g_running = true;
    g_thread = std::thread(WatchThread);
    RespondOk(res, "null");
}

void monitorStop(const ow_request_t*, ow_response_t* res) {
    if (!g_running.load()) return RespondOk(res, "null");
    g_running = false;
    if (g_hwnd) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    if (g_thread.joinable()) g_thread.join();
    RespondOk(res, "null");
}

// ── idle / batería ──────────────────────────────────────────────────────────
void idleTime(const ow_request_t*, ow_response_t* res) {
    LASTINPUTINFO li{};
    li.cbSize = sizeof(li);
    DWORD ms = 0;
    if (GetLastInputInfo(&li)) ms = GetTickCount() - li.dwTime;
    RespondOk(res, Value(static_cast<int64_t>(ms / 1000)).Serialize().c_str());
}

void idleState(const ow_request_t* req, ow_response_t* res) {
    int threshold = 0;
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsNumber())
        threshold = static_cast<int>(parsed.value->AsArray()[0].AsInt());
    if (g_locked) return RespondOk(res, "\"locked\"");
    LASTINPUTINFO li{};
    li.cbSize = sizeof(li);
    DWORD ms = 0;
    if (GetLastInputInfo(&li)) ms = GetTickCount() - li.dwTime;
    const bool idle = static_cast<int>(ms / 1000) >= threshold;
    RespondOk(res, idle ? "\"idle\"" : "\"active\"");
}

void isOnBattery(const ow_request_t*, ow_response_t* res) {
    RespondOk(res, ReadOnBattery() ? "true" : "false");
}

// ── inhibidores ─────────────────────────────────────────────────────────────
void inhibitStart(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    std::string what = "prevent-display-sleep";
    if (parsed.value && parsed.value->IsArray() &&
        !parsed.value->AsArray().empty() && parsed.value->AsArray()[0].IsString())
        what = parsed.value->AsArray()[0].AsString();

    std::lock_guard lock(s_mu);
    const int id = s_nextInhibit++;
    s_inhibits[id] = (what == "prevent-app-suspension");
    ApplyUnion();
    RespondOk(res, Value(static_cast<int64_t>(id)).Serialize().c_str());
}

void inhibitStop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "inhibitId requerido");
    const int id = static_cast<int>(parsed.value->AsArray()[0].AsInt());

    std::lock_guard lock(s_mu);
    const bool removed = s_inhibits.erase(id) > 0;
    if (removed) ApplyUnion();
    RespondOk(res, removed ? "true" : "false");
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
    static const ow_module_desc_t d{"power", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
