// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/safestorage/src/safestorage_linux.cpp — almacenamiento cifrado.
//
// AES-256-GCM con una clave aleatoria de 32 bytes guardada en el data dir de la
// app con permisos 0600. Formato del blob: "ow1:" + base64(iv[12] | tag[16] | ct).
// `decrypt` acepta texto plano (sin prefijo) para compatibilidad con valores
// guardados cuando no había backend (equivalente a encrypted:false).
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <sys/stat.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ss {

using ow::Module::RespondError;
using ow::Module::RespondOk;

static const char* kPrefix = "ow1:";

static std::string DataDir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::string base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = std::string(home && *home ? home : ".") + "/.local/share";
    }
    const char* id = std::getenv("OW_APP_ID");
    if (!id || !*id) id = std::getenv("OW_APP_NAME");
    if (!id || !*id) id = "owear";
    return base + "/owear/" + id;
}

/// Carga (o crea, 0600) la clave local de 32 bytes.
static bool LoadKey(unsigned char out[32]) {
    const std::string dir = DataDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/safe-key.bin";

    std::ifstream in(path, std::ios::binary);
    if (in) {
        in.read(reinterpret_cast<char*>(out), 32);
        if (in.gcount() == 32) return true;
    }
    if (RAND_bytes(out, 32) != 1) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(out), 32);
    file.close();
    ::chmod(path.c_str(), 0600);
    return true;
}

static std::string B64(const unsigned char* data, size_t len) {
    if (len == 0) return {};
    const int outLen = 4 * static_cast<int>((len + 2) / 3);
    std::string out(static_cast<size_t>(outLen), '\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&out[0]), data,
                    static_cast<int>(len));
    return out;
}

static bool UnB64(const std::string& in, std::vector<unsigned char>& out) {
    if (in.empty() || in.size() % 4 != 0) return false;
    out.resize(in.size() / 4 * 3);
    const int n = EVP_DecodeBlock(out.data(),
                                  reinterpret_cast<const unsigned char*>(in.data()),
                                  static_cast<int>(in.size()));
    if (n < 0) return false;
    // EVP_DecodeBlock no descuenta el padding.
    size_t len = static_cast<size_t>(n);
    if (!in.empty() && in[in.size() - 1] == '=') len--;
    if (in.size() > 1 && in[in.size() - 2] == '=') len--;
    out.resize(len);
    return true;
}

void isAvailable(const ow_request_t*, ow_response_t* res) { RespondOk(res, "true"); }

void encrypt(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "se espera [texto]");
    const std::string plain = parsed.value->AsArray()[0].AsString();

    unsigned char key[32];
    if (!LoadKey(key)) return RespondError(res, "no se pudo obtener la clave local");

    unsigned char iv[12], tag[16];
    if (RAND_bytes(iv, 12) != 1) return RespondError(res, "RAND falló");

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return RespondError(res, "sin contexto EVP");
    std::vector<unsigned char> ct(plain.size() + 16);
    int len = 0, total = 0;
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, iv);
    EVP_EncryptUpdate(ctx, ct.data(), &len,
                      reinterpret_cast<const unsigned char*>(plain.data()),
                      static_cast<int>(plain.size()));
    total = len;
    EVP_EncryptFinal_ex(ctx, ct.data() + len, &len);
    total += len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
    EVP_CIPHER_CTX_free(ctx);
    ct.resize(static_cast<size_t>(total));

    std::vector<unsigned char> blob;
    blob.reserve(12 + 16 + ct.size());
    blob.insert(blob.end(), iv, iv + 12);
    blob.insert(blob.end(), tag, tag + 16);
    blob.insert(blob.end(), ct.begin(), ct.end());

    std::string data = kPrefix + B64(blob.data(), blob.size());
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

    // Sin prefijo → texto plano (se guardó cuando no había backend).
    if (data.rfind(kPrefix, 0) != 0)
        return RespondOk(res, ow::json::Value(data).Serialize().c_str());

    std::vector<unsigned char> blob;
    if (!UnB64(data.substr(4), blob) || blob.size() < 28)
        return RespondError(res, "blob inválido");

    unsigned char key[32];
    if (!LoadKey(key)) return RespondError(res, "no se pudo obtener la clave local");

    const unsigned char* iv = blob.data();
    const unsigned char* tag = blob.data() + 12;
    const unsigned char* ct = blob.data() + 28;
    const int ctLen = static_cast<int>(blob.size() - 28);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return RespondError(res, "sin contexto EVP");
    std::vector<unsigned char> plain(ctLen + 16);
    int len = 0, total = 0;
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, iv);
    EVP_DecryptUpdate(ctx, plain.data(), &len, ct, ctLen);
    total = len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<unsigned char*>(tag));
    const int ok = EVP_DecryptFinal_ex(ctx, plain.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);
    if (ok <= 0) return RespondError(res, "no se pudo descifrar (clave o blob inválidos)");
    total += len;
    const std::string text(reinterpret_cast<char*>(plain.data()), static_cast<size_t>(total));
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
