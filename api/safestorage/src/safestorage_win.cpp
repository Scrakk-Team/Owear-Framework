// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/safestorage/src/safestorage_win.cpp — almacenamiento cifrado (DPAPI).
//
// CryptProtectData/CryptUnprotectData cifran con la cuenta del usuario; el blob
// se guarda como base64. `decrypt` acepta texto plano (sin descifrar) para
// compatibilidad con valores guardados cuando no había backend.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

#include <string>
#include <vector>

namespace ss {

using ow::Module::RespondError;
using ow::Module::RespondOk;

static std::string B64(const BYTE* data, DWORD len) {
    DWORD chars = 0;
    if (!CryptBinaryToStringA(data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              nullptr, &chars))
        return {};
    std::string out(chars ? chars : 0, '\0');
    if (!CryptBinaryToStringA(data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              &out[0], &chars))
        return {};
    if (chars > 0 && out[chars - 1] == '\0') out.resize(chars - 1);
    return out;
}

static bool UnB64(const std::string& in, std::vector<BYTE>& out) {
    DWORD bytes = 0;
    if (!CryptStringToBinaryA(in.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &bytes,
                              nullptr, nullptr))
        return false;
    out.resize(bytes);
    if (!CryptStringToBinaryA(in.c_str(), 0, CRYPT_STRING_BASE64, out.data(), &bytes,
                              nullptr, nullptr))
        return false;
    out.resize(bytes);
    return true;
}

void isAvailable(const ow_request_t*, ow_response_t* res) { RespondOk(res, "true"); }

void encrypt(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "se espera [texto]");
    const std::string plain = parsed.value->AsArray()[0].AsString();

    DATA_BLOB in{static_cast<DWORD>(plain.size()),
                 reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"owear", nullptr, nullptr, nullptr, 0, &out))
        return RespondError(res, "CryptProtectData falló");
    const std::string data = B64(out.pbData, out.cbData);
    LocalFree(out.pbData);

    ow::json::Object o;
    o.emplace_back("data", ow::json::Value(data));
    o.emplace_back("encrypted", ow::json::Value(true));
    RespondOk(res, ow::json::Value(std::move(o)).Serialize().c_str());
}

void decrypt(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "se espera [data]");
    const std::string data = parsed.value->AsArray()[0].AsString();

    std::vector<BYTE> blob;
    if (!UnB64(data, blob) || blob.empty()) {
        // No es base64 DPAPI → texto plano.
        return RespondOk(res, ow::json::Value(data).Serialize().c_str());
    }

    DATA_BLOB in{static_cast<DWORD>(blob.size()), blob.data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        // No se pudo descifrar → asumir texto plano.
        return RespondOk(res, ow::json::Value(data).Serialize().c_str());
    }
    const std::string text(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    RespondOk(res, ow::json::Value(text).Serialize().c_str());
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
