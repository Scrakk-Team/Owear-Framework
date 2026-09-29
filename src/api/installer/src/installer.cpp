// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/api/installer/src/installer.cpp — builtin "installer".
//
// El instalador es un binario Owear aparte. Detecta su modo con `OW_MODE`
// (installer | uninstaller | app) y lee SU PROPIO payload embebido (footer
// OWPK1) para planificar y desplegar la app:
//
//   payload/        → lo que se instala (binario único [minimal] o árbol [layout])
//   bridge.json     → contrato app↔installer (defineBridge), embebido en build
//   installer.json  → metadatos de la app + modo/orden/presets
//   ui/             → renderer del instalador (lo sirve el kernel como assets)
//
#include "../../../Pack/Pack.hpp"
#include "../../../Runtime/Sha256.hpp"
#include "Internal.hpp"
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace installermod {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

namespace fs = std::filesystem;

// ── helpers de argumentos / respuesta ───────────────────────────────────────

static Value Args(const ow_request_t* req) {
    if (!req || !req->json || req->json_len == 0) return Value(nullptr);
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return Value(nullptr);
    const Value& v = *parsed.value;
    // El dispatcher pasa los argumentos como ARRAY: usamos el primero (objeto).
    if (v.IsArray()) {
        const auto& arr = v.AsArray();
        return arr.empty() ? Value(nullptr) : arr[0];
    }
    return v;
}

static std::string Str(const Value& o, const char* k, std::string def = {}) {
    const Value* v = o.Find(k);
    return (v && v->IsString()) ? v->AsString() : def;
}

static bool Bool(const Value& o, const char* k, bool def = false) {
    const Value* v = o.Find(k);
    return (v && v->IsBool()) ? v->AsBool() : def;
}

static void Ok(ow_response_t* res, const Value& v) { RespondOk(res, v.Serialize()); }

static const char* EnvOr(const char* key, const char* def) {
    const char* v = std::getenv(key);
    return (v && *v) ? v : def;
}

static std::string AppId() { return EnvOr("OW_APP_ID", "default"); }
static std::string AppName() { return EnvOr("OW_APP_NAME", "Owear App"); }

/// Raíz del payload embebido (extraído a caché, idempotente).
static std::string PayloadRoot() { return ow::pack::EnsureExtracted(); }

/// Carpeta con lo que se instala: `<root>/payload` (o la raíz si no existe).
static std::string PayloadDir() {
    const std::string root = PayloadRoot();
    if (root.empty()) return {};
    std::error_code ec;
    const std::string sub = root + "/payload";
    return fs::is_directory(sub, ec) ? sub : root;
}

static std::string Sha256File(const fs::path& p) {
    ow::crypto::Sha256 h;
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
        h.Update(reinterpret_cast<const uint8_t*>(buf),
                 static_cast<size_t>(f.gcount()));
    return h.Hex();
}

static bool IsExecutable(const std::string& rel) {
    // Heurística: sin extensión o dentro de bin/ → ejecutable.
    const std::string name = fs::path(rel).filename().string();
    if (rel.rfind("bin/", 0) == 0) return true;
    return name.find('.') == std::string::npos;
}

// ── construcción del plan ───────────────────────────────────────────────────

/// Lista recursivamente el payload (rel, tamaño, si es dir).
static std::vector<PlanEntry> ListPayload() {
    std::vector<PlanEntry> out;
    const std::string dir = PayloadDir();
    if (dir.empty()) return out;
    std::error_code ec;
    auto opts = fs::directory_options::skip_permission_denied;
    for (auto it = fs::recursive_directory_iterator(dir, opts, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string rel = fs::relative(it->path(), dir, ec).generic_string();
        if (rel.empty() || rel == ".") continue;
        PlanEntry e;
        e.rel = rel;
        e.dir = it->is_directory(ec);
        e.size = e.dir ? 0 : static_cast<uint64_t>(it->file_size(ec));
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(),
              [](const PlanEntry& a, const PlanEntry& b) { return a.rel < b.rel; });
    return out;
}

/// Aplica `layout` (flat = basename; tree = ruta relativa) y `order` (prefijos).
static void MapDest(std::vector<PlanEntry>& in, const std::string& layout,
                    const std::vector<std::string>& order) {
    for (auto& e : in)
        e.dst = (layout == "flat") ? fs::path(e.rel).filename().string() : e.rel;
    if (order.empty()) return;
    auto rank = [&](const PlanEntry& e) -> size_t {
        for (size_t i = 0; i < order.size(); ++i) {
            const std::string& pre = order[i];
            if (e.rel == pre || e.rel.rfind(pre + "/", 0) == 0 ||
                e.dst == pre || e.dst.rfind(pre + "/", 0) == 0)
                return i;
        }
        return order.size();
    };
    std::stable_sort(in.begin(), in.end(),
                     [&](const PlanEntry& a, const PlanEntry& b) { return rank(a) < rank(b); });
}

static std::vector<std::string> StrArray(const Value& o, const char* k) {
    std::vector<std::string> out;
    const Value* v = o.Find(k);
    if (v && v->IsArray())
        for (const auto& x : v->AsArray())
            if (x.IsString()) out.push_back(x.AsString());
    return out;
}

static Value PlanToJson(const std::vector<PlanEntry>& plan) {
    Array arr;
    for (const auto& e : plan) {
        Object o;
        o.emplace_back("rel", Value(e.rel));
        o.emplace_back("dst", Value(e.dst));
        o.emplace_back("size", Value(static_cast<int64_t>(e.size)));
        o.emplace_back("dir", Value(e.dir));
        arr.emplace_back(Value(std::move(o)));
    }
    return Value(std::move(arr));
}

// ── comandos ────────────────────────────────────────────────────────────────

static void mode(const ow_request_t*, ow_response_t* res) {
    Ok(res, Value(std::string(EnvOr("OW_MODE", "app"))));
}

static void info(const ow_request_t*, ow_response_t* res) {
    const std::string root = PayloadRoot();
    std::ifstream f(root + "/installer.json", std::ios::binary);
    if (f) {
        std::string raw((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
        RespondOk(res, raw.empty() ? "{}" : raw);
        return;
    }
    Object o;
    o.emplace_back("appId", Value(AppId()));
    o.emplace_back("appName", Value(AppName()));
    o.emplace_back("version", Value(std::string(EnvOr("OW_APP_VERSION", "0.0.0"))));
    o.emplace_back("mode", Value(std::string(EnvOr("OW_MODE", "app"))));
    Ok(res, Value(std::move(o)));
}

static void bridge(const ow_request_t*, ow_response_t* res) {
    std::ifstream f(PayloadRoot() + "/bridge.json", std::ios::binary);
    if (!f) { RespondOk(res, "null"); return; }
    std::string raw((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    RespondOk(res, raw.empty() ? "null" : raw);
}

static void payloadList(const ow_request_t*, ow_response_t* res) {
    Ok(res, PlanToJson(ListPayload()));
}

static void payloadRead(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    const std::string rel = Str(a, "path");
    const std::string dir = PayloadDir();
    if (rel.empty() || dir.empty()) { RespondError(res, "path requerido"); return; }
    fs::path p = fs::weakly_canonical(fs::path(dir) / rel);
    fs::path base = fs::weakly_canonical(fs::path(dir));
    if (p.string().rfind(base.string(), 0) != 0) { RespondError(res, "ruta fuera del payload"); return; }
    std::ifstream f(p, std::ios::binary);
    if (!f) { RespondError(res, "no existe: " + rel); return; }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Object o;
    o.emplace_back("path", Value(rel));
    o.emplace_back("data", Value(ow::b64::Encode(data)));
    o.emplace_back("size", Value(static_cast<int64_t>(data.size())));
    Ok(res, Value(std::move(o)));
}

static void plan(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    const std::string modeArg = Str(a, "mode", "minimal");
    const std::string layout = Str(a, "layout", modeArg == "minimal" ? "flat" : "tree");
    auto entries = ListPayload();
    MapDest(entries, layout, StrArray(a, "order"));
    Ok(res, PlanToJson(entries));
}

static void install(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    const std::string dir = Str(a, "dir");
    if (dir.empty()) { RespondError(res, "dir requerido"); return; }
    const std::string payload = PayloadDir();
    if (payload.empty()) { RespondError(res, "sin payload embebido"); return; }
    const std::string modeArg = Str(a, "mode", "minimal");
    const std::string layout = Str(a, "layout", modeArg == "minimal" ? "flat" : "tree");

    auto entries = ListPayload();
    MapDest(entries, layout, StrArray(a, "order"));

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) { RespondError(res, "no se pudo crear " + dir); return; }

    Array done;
    for (const auto& e : entries) {
        const fs::path src = fs::path(payload) / e.rel;
        const fs::path dst = fs::path(dir) / e.dst;
        if (e.dir) {
            fs::create_directories(dst, ec);
            continue;
        }
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) { RespondError(res, "fallo copiando " + e.rel + ": " + ec.message()); return; }
        fs::permissions(dst,
                        IsExecutable(e.dst)
                            ? (fs::perms::owner_all | fs::perms::group_read |
                               fs::perms::group_exec | fs::perms::others_read |
                               fs::perms::others_exec)
                            : (fs::perms::owner_read | fs::perms::owner_write |
                               fs::perms::group_read | fs::perms::others_read),
                        fs::perm_options::replace, ec);
        Object fe;
        fe.emplace_back("path", Value(e.dst));
        fe.emplace_back("size", Value(static_cast<int64_t>(e.size)));
        fe.emplace_back("sha256", Value(Sha256File(dst)));
        done.emplace_back(Value(std::move(fe)));
    }

    // manifiesto de instalación
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const int64_t stamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    Object man;
    man.emplace_back("app", Value(AppName()));
    man.emplace_back("appId", Value(AppId()));
    man.emplace_back("version", Value(std::string(EnvOr("OW_APP_VERSION", "0.0.0"))));
    man.emplace_back("mode", Value(modeArg));
    man.emplace_back("layout", Value(layout));
    man.emplace_back("dir", Value(dir));
    man.emplace_back("installedAt", Value(stamp));
    man.emplace_back("entries", Value(std::move(done)));
    {
        std::ofstream out(fs::path(dir) / ".owear-install.json", std::ios::binary | std::ios::trunc);
        if (out) out << Value(std::move(man)).Serialize();
    }

    Object r;
    r.emplace_back("installed", Value(true));
    r.emplace_back("dir", Value(dir));
    r.emplace_back("mode", Value(modeArg));
    r.emplace_back("files", Value(static_cast<int64_t>(entries.size())));
    Ok(res, Value(std::move(r)));
}

static void uninstall(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    const std::string dir = Str(a, "dir");
    const bool keepData = Bool(a, "keepData", false);
    (void)keepData; // reservado: datos de usuario en la caché se conservan siempre en v1
    if (dir.empty()) { RespondError(res, "dir requerido"); return; }

    std::ifstream f(fs::path(dir) / ".owear-install.json", std::ios::binary);
    if (!f) { RespondError(res, "no instalado en " + dir); return; }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto parsed = ow::json::Parse(raw);
    const Value man = parsed.value ? *parsed.value : Value(nullptr);

    int removed = 0;
    std::error_code ec;
    if (const Value* entries = man.Find("entries"); entries && entries->IsArray()) {
        for (const auto& e : entries->AsArray()) {
            const Value* p = e.Find("path");
            if (!p || !p->IsString()) continue;
            const fs::path target = fs::path(dir) / p->AsString();
            if (fs::remove(target, ec)) ++removed;
            // limpia directorios vacíos hacia arriba
            for (fs::path up = target.parent_path();
                 !up.empty() && up != fs::path(dir); up = up.parent_path()) {
                std::error_code e2;
                if (!fs::remove(up, e2)) break;
            }
        }
    }
    fs::remove(fs::path(dir) / ".owear-install.json", ec);
    {
        std::error_code e4;
        if (fs::is_empty(dir, e4)) fs::remove(dir, e4);
    }
    platform::RemoveShortcuts(AppId(), AppName());
    platform::UnregisterUninstall(AppId());

    Object r;
    r.emplace_back("removed", Value(removed));
    r.emplace_back("dir", Value(dir));
    Ok(res, Value(std::move(r)));
}

static void verify(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    const std::string dir = Str(a, "dir");
    if (dir.empty()) { RespondError(res, "dir requerido"); return; }
    std::ifstream f(fs::path(dir) / ".owear-install.json", std::ios::binary);
    if (!f) { RespondError(res, "no instado en " + dir); return; }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto parsed = ow::json::Parse(raw);
    const Value man = parsed.value ? *parsed.value : Value(nullptr);

    Array mism;
    bool all = true;
    if (const Value* entries = man.Find("entries"); entries && entries->IsArray()) {
        for (const auto& e : entries->AsArray()) {
            const Value* p = e.Find("path");
            const Value* h = e.Find("sha256");
            if (!p || !p->IsString()) continue;
            const std::string want = (h && h->IsString()) ? h->AsString() : "";
            const std::string got = Sha256File(fs::path(dir) / p->AsString());
            if (!want.empty() && want != got) {
                all = false;
                mism.emplace_back(Value(p->AsString()));
            }
        }
    }
    Object r;
    r.emplace_back("ok", Value(all));
    r.emplace_back("mismatches", Value(std::move(mism)));
    Ok(res, Value(std::move(r)));
}

static void state(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    const std::string dir = Str(a, "dir");
    std::ifstream f(fs::path(dir) / ".owear-install.json", std::ios::binary);
    if (!f) {
        Object r;
        r.emplace_back("installed", Value(false));
        Ok(res, Value(std::move(r)));
        return;
    }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto parsed = ow::json::Parse(raw);
    Value man = parsed.value ? *parsed.value : Value(nullptr);
    if (man.IsObject()) {
        Object r;
        r.emplace_back("installed", Value(true));
        if (const Value* v = man.Find("version"); v && v->IsString())
            r.emplace_back("version", *v);
        if (const Value* v = man.Find("mode"); v && v->IsString()) r.emplace_back("mode", *v);
        if (const Value* v = man.Find("app"); v && v->IsString()) r.emplace_back("app", *v);
        Ok(res, Value(std::move(r)));
        return;
    }
    Object r;
    r.emplace_back("installed", Value(true));
    Ok(res, Value(std::move(r)));
}

static void shortcuts(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    const bool ok = platform::CreateShortcuts(
        AppId(), AppName(), Str(a, "execPath"), Str(a, "iconPath"),
        Bool(a, "desktop", true), Bool(a, "menu", true), Bool(a, "startup", false));
    Ok(res, Value(ok));
}

static void launch(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    const bool ok = platform::LaunchDetached(Str(a, "path"), StrArray(a, "args"));
    Ok(res, Value(ok));
}

static void elevate(const ow_request_t*, ow_response_t* res) {
    Object o;
    o.emplace_back("elevated", Value(platform::IsElevated()));
    Ok(res, Value(std::move(o)));
}

} // namespace installermod

namespace ow::internal {
const ow_module_desc_t* InstallerModuleDescriptor() {
    static const ow_fn_entry_t fns[] = {
        {"mode", &installermod::mode},
        {"info", &installermod::info},
        {"bridge", &installermod::bridge},
        {"payloadList", &installermod::payloadList},
        {"payloadRead", &installermod::payloadRead},
        {"plan", &installermod::plan},
        {"install", &installermod::install},
        {"uninstall", &installermod::uninstall},
        {"verify", &installermod::verify},
        {"state", &installermod::state},
        {"shortcuts", &installermod::shortcuts},
        {"launch", &installermod::launch},
        {"elevate", &installermod::elevate},
    };
    static const ow_module_desc_t d{"installer", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
} // namespace ow::internal
