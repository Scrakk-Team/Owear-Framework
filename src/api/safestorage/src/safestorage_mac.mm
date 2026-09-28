// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/safestorage/src/safestorage_mac.mm — stub (macOS fuera de 0.1.x).
// Devuelve texto plano con encrypted:false (equivalente a "sin backend").
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <string>

namespace ss {

using ow::Module::RespondOk;

void isAvailable(const ow_request_t*, ow_response_t* res) { RespondOk(res, "false"); }

void encrypt(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    std::string plain;
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty())
        plain = parsed.value->AsArray()[0].AsString();
    ow::json::Object o;
    o.emplace_back("data", ow::json::Value(plain));
    o.emplace_back("encrypted", ow::json::Value(false));
    RespondOk(res, ow::json::Value(std::move(o)).Serialize().c_str());
}

void decrypt(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    std::string data;
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty())
        data = parsed.value->AsArray()[0].AsString();
    RespondOk(res, ow::json::Value(data).Serialize().c_str());
}

} // namespace ss

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"isAvailable", &ss::isAvailable},
        {"encrypt", &ss::encrypt},
        {"decrypt", &ss::decrypt},
    };
    static const ow_module_desc_t d{"safestorage", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
