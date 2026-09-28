// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// benchmarks/owear/native/bench.cpp — módulo nativo de benchmark.
// echo (round-trip IPC), burst (N eventos nativo→renderer), ready (marca de
// arranque) y report (escribe el JSON de resultados).
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <cstdio>
#include <fstream>
#include <string>

namespace bn {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;

void echo(const ow_request_t* req, ow_response_t* res) {
    auto p = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (p.value && p.value->IsArray() && !p.value->AsArray().empty())
        return RespondOk(res, p.value->AsArray()[0].Serialize().c_str());
    RespondOk(res, "null");
}

void burst(const ow_request_t* req, ow_response_t* res) {
    auto p = ow::json::Parse(std::string_view(req->json, req->json_len));
    int n = 5000;
    if (p.value && p.value->IsArray() && !p.value->AsArray().empty() &&
        p.value->AsArray()[0].IsNumber())
        n = static_cast<int>(p.value->AsArray()[0].AsInt());
    if (g_host && g_host->emit_event) {
        for (int i = 0; i < n; ++i)
            g_host->emit_event(g_host->ctx, req->window_id, "bench-tick", "null");
    }
    RespondOk(res, "null");
}

void ready(const ow_request_t*, ow_response_t* res) {
    std::fprintf(stdout, "BENCH_READY\n");
    std::fflush(stdout);
    RespondOk(res, "null");
}

void report(const ow_request_t* req, ow_response_t* res) {
    auto p = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!p.value || !p.value->IsArray() || p.value->AsArray().size() < 2)
        return RespondError(res, "report(path, json)");
    const std::string path = p.value->AsArray()[0].AsString();
    const std::string json = p.value->AsArray()[1].AsString();
    std::ofstream f(path, std::ios::binary);
    if (!f) return RespondError(res, "no se pudo abrir " + path);
    f << json;
    f.close();
    RespondOk(res, "null");
}

void mark(const ow_request_t* req, ow_response_t* res) {
    auto p = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!p.value || !p.value->IsArray() || p.value->AsArray().empty())
        return RespondError(res, "mark(path)");
    std::ofstream f(p.value->AsArray()[0].AsString(), std::ios::binary);
    f << "1";
    RespondOk(res, "null");
}

} // namespace bn

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    bn::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"echo", &bn::echo},
        {"burst", &bn::burst},
        {"ready", &bn::ready},
        {"report", &bn::report},
        {"mark", &bn::mark},
    };
    static const ow_module_desc_t d{"bench", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
