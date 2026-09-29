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
#include "../../../Control/ControlServer.hpp"
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

std::string ExpandPath(const std::string& in) {
    if (in.empty()) return in;
    const char* home =
#ifdef _WIN32
        std::getenv("USERPROFILE");
#else
        std::getenv("HOME");
#endif
    std::string out = in;
    if ((out == "~" || out.rfind("~/", 0) == 0 || out.rfind("~\\", 0) == 0) && home && *home)
        out = std::string(home) + (out.size() > 1 ? out.substr(1) : std::string());
    std::error_code ec;
    auto abs = fs::absolute(out, ec);
    return ec ? out : abs.lexically_normal().string();
}

/// Raíz del payload embebido (extraído a caché, idempotente).
static std::string PayloadRoot() { return ow::pack::EnsureExtracted(); }

// ── metadatos (installer.json): protect + hooks ─────────────────────────────

struct InstallerMeta {
    bool valid = false;
    Value root{nullptr};
};

static const InstallerMeta& Meta() {
    static const InstallerMeta m = [] {
        InstallerMeta r;
        std::ifstream f(PayloadRoot() + "/installer.json", std::ios::binary);
        if (f) {
            std::string raw((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
            auto parsed = ow::json::Parse(raw);
            if (parsed.value) {
                r.root = *parsed.value;
                r.valid = true;
            }
        }
        return r;
    }();
    return m;
}

/// Identificador para nombres de fichero/rutas (appId → slug seguro).
static std::string Slug(const std::string& in) {
    std::string out;
    for (char c : in) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')
            out += c;
        else if (c >= 'A' && c <= 'Z')
            out += static_cast<char>(c - 'A' + 'a');
        else if (c == ' ')
            out += '-';
    }
    return out.empty() ? "app" : out;
}

static std::string AppSlug() {
    std::string s;
    const auto& m = Meta();
    if (m.valid) {
        if (const Value* n = m.root.Find("appName"); n && n->IsString())
            s = n->AsString();
        else if (const Value* v = m.root.Find("appId"); v && v->IsString())
            s = v->AsString();
    }
    if (s.empty()) s = AppName();
    return Slug(s);
}

static std::string AppIdSlug() { return Slug(AppId()); }

// ── registro de apps INSTALADAS (a nivel de usuario) ────────────────────────
//
// Es una capacidad de Owear (builtin installer): apunta dónde quedó cada app
// para que `state`/`uninstall`/el desinstalador la encuentren SIN pedir la ruta.
//   Linux:   $XDG_CONFIG_HOME|~/.config/owear/installed/<appId>.json
//   Windows: %APPDATA%\owear\installed\<appId>.json  (+ HKCU Uninstall)

static std::string RegistryDir() {
#ifdef _WIN32
    const char* ad = std::getenv("APPDATA");
    const std::string base = (ad && *ad) ? std::string(ad) : ExpandPath("~");
    return base + "\\owear\\installed";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const std::string base = (xdg && *xdg) ? std::string(xdg) : ExpandPath("~/.config");
    return base + "/owear/installed";
#endif
}

static std::string RegistryFile(const std::string& appId) {
#ifdef _WIN32
    return RegistryDir() + "\\" + Slug(appId) + ".json";
#else
    return RegistryDir() + "/" + Slug(appId) + ".json";
#endif
}

static void RegistryWrite(const std::string& dir, const std::string& mode) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const int64_t stamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    Object o;
    o.emplace_back("appId", Value(AppId()));
    o.emplace_back("appName", Value(AppName()));
    o.emplace_back("version", Value(std::string(EnvOr("OW_APP_VERSION", "0.0.0"))));
    o.emplace_back("dir", Value(dir));
    o.emplace_back("mode", Value(mode));
    o.emplace_back("installedAt", Value(stamp));
    std::error_code ec;
    fs::create_directories(RegistryDir(), ec);
    std::ofstream f(RegistryFile(AppId()), std::ios::binary | std::ios::trunc);
    if (f) f << Value(std::move(o)).Serialize();
}

/// Entrada del registro para `appId` (o null si no hay).
static Value RegistryRead(const std::string& appId) {
    std::ifstream f(RegistryFile(appId), std::ios::binary);
    if (!f) return Value(nullptr);
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto parsed = ow::json::Parse(raw);
    return (parsed.value && parsed.value->IsObject()) ? *parsed.value : Value(nullptr);
}

static void RegistryRemove(const std::string& appId) {
    std::error_code ec;
    fs::remove(RegistryFile(appId), ec);
}

static Value RegistryList() {
    Array arr;
    std::error_code ec;
    for (auto it = fs::directory_iterator(RegistryDir(), ec);
         it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->path().extension() != ".json") continue;
        std::ifstream f(it->path(), std::ios::binary);
        if (!f) continue;
        std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto parsed = ow::json::Parse(raw);
        if (parsed.value && parsed.value->IsObject()) arr.emplace_back(*parsed.value);
    }
    return Value(std::move(arr));
}

/// Grupo lógico de una ruta del payload (para `protect`).
static std::string GroupForRel(const std::string& rel) {
    const auto inDir = [&](const char* d) {
        return rel == d || rel.rfind(std::string(d) + "/", 0) == 0;
    };
    if (inDir("app")) return "app:assets";
    if (inDir("workers")) return "app:workers";
    if (rel == "main.js") return "app:main";
    if (inDir("modules")) return "modules:app";
    if (inDir("resources")) return "resources";
    if (inDir("node")) return "node";
    return "kernel";
}

static bool ProtectFlag(const std::string& rel, const char* flag) {
    const auto& m = Meta();
    if (!m.valid) return false;
    const Value* pr = m.root.Find("protect");
    if (!pr || !pr->IsObject()) return false;
    const Value* g = pr->Find(GroupForRel(rel));
    if (!g || !g->IsObject()) return false;
    const Value* f = g->Find(flag);
    return f && f->IsBool() && f->AsBool();
}

static void EmitHook(const char* phase) {
    const auto& m = Meta();
    if (!m.valid) return;
    const Value* h = m.root.Find("hooks");
    if (!h || !h->IsObject()) return;
    const Value* name = h->Find(phase);
    if (!name || !name->IsString()) return;
    Object o;
    o.emplace_back("phase", Value(std::string(phase)));
    o.emplace_back("hook", *name);
    ow::ControlServer::Get().BroadcastEvent("installer.hook",
                                        Value(std::move(o)).Serialize());
}

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
        if (ProtectFlag(rel, "hidden")) continue; // protegido: no se lista
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
    Value base(nullptr);
    for (const char* name : {"/installer.json", "/uninstaller.json"}) {
        std::ifstream f(root + name, std::ios::binary);
        if (!f) continue;
        std::string raw((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
        auto parsed = ow::json::Parse(raw);
        if (parsed.value && parsed.value->IsObject()) {
            base = *parsed.value;
            break;
        }
    }

    Object o;
    if (base.IsObject()) {
        for (const auto& m : base.AsObject()) o.emplace_back(m.first, m.second);
    } else {
        o.emplace_back("appId", Value(AppId()));
        o.emplace_back("appName", Value(AppName()));
        o.emplace_back("version", Value(std::string(EnvOr("OW_APP_VERSION", "0.0.0"))));
        o.emplace_back("mode", Value(std::string(EnvOr("OW_MODE", "app"))));
    }

    // Estado real de instalación (registro por usuario) → dir / installed.
    const Value reg = RegistryRead(AppId());
    if (reg.IsObject()) {
        o.emplace_back("installed", Value(true));
        if (const Value* d = reg.Find("dir"); d && d->IsString()) o.emplace_back("dir", *d);
        if (const Value* v = reg.Find("version"); v && v->IsString()) o.emplace_back("version", *v);
    } else {
        o.emplace_back("installed", Value(false));
    }
    Ok(res, Value(std::move(o)));
}

/// Lista las apps instaladas (registro).
static void list(const ow_request_t*, ow_response_t* res) { Ok(res, RegistryList()); }

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
    const std::string dir = ExpandPath(Str(a, "dir"));
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

    EmitHook("preInstall");

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
                        ([&] {
                            const bool ex = IsExecutable(e.dst);
                            if (ProtectFlag(e.dst, "readonly"))
                                return ex
                                           ? (fs::perms::owner_read | fs::perms::owner_exec |
                                              fs::perms::group_read | fs::perms::group_exec |
                                              fs::perms::others_read | fs::perms::others_exec)
                                           : (fs::perms::owner_read | fs::perms::group_read |
                                              fs::perms::others_read);
                            return ex
                                       ? (fs::perms::owner_all | fs::perms::group_read |
                                          fs::perms::group_exec | fs::perms::others_read |
                                          fs::perms::others_exec)
                                       : (fs::perms::owner_read | fs::perms::owner_write |
                                          fs::perms::group_read | fs::perms::others_read);
                        })(),
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

    // Registro de desinstalación del S.O. (Windows: Add/Remove Programs; Linux: no-op).
    const std::string uninstaller = Str(a, "uninstaller");
    if (!uninstaller.empty()) {
        platform::RegisterUninstall(AppId(), AppName(),
                                    Str(a, "version", std::string(EnvOr("OW_APP_VERSION", "0.0.0"))),
                                    Str(a, "publisher"), dir, uninstaller);
    }

    // Registro de apps instaladas (Owear) → el desinstalador/`state` la encuentran.
    RegistryWrite(dir, modeArg);

    EmitHook("postInstall");

    Object r;
    r.emplace_back("installed", Value(true));
    r.emplace_back("dir", Value(dir));
    r.emplace_back("mode", Value(modeArg));
    r.emplace_back("files", Value(static_cast<int64_t>(entries.size())));
    Ok(res, Value(std::move(r)));
}

static void uninstall(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    std::string dir = ExpandPath(Str(a, "dir"));
    const bool keepData = Bool(a, "keepData", false);
    (void)keepData; // reservado: datos de usuario en la caché se conservan siempre en v1
    // Sin dir: usar el registrado (así el desinstalador no necesita la ruta).
    if (dir.empty()) {
        const Value reg = RegistryRead(AppId());
        if (reg.IsObject())
            if (const Value* d = reg.Find("dir"); d && d->IsString()) dir = d->AsString();
    }
    if (dir.empty()) { RespondError(res, "no hay instalación registrada"); return; }

    EmitHook("preUninstall");

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
    RegistryRemove(AppId());

    EmitHook("postUninstall");

    Object r;
    r.emplace_back("removed", Value(removed));
    r.emplace_back("dir", Value(dir));
    Ok(res, Value(std::move(r)));
}

static void verify(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    const std::string dir = ExpandPath(Str(a, "dir"));
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
    std::string dir = ExpandPath(Str(a, "dir"));
    if (dir.empty()) {
        const Value reg = RegistryRead(AppId());
        if (reg.IsObject())
            if (const Value* d = reg.Find("dir"); d && d->IsString()) dir = d->AsString();
    }
    std::ifstream f(fs::path(dir) / ".owear-install.json", std::ios::binary);
    if (!f) {
        // Fallback: el registro por usuario (aunque el dir no se pueda leer).
        const Value reg = RegistryRead(AppId());
        Object r;
        if (reg.IsObject()) {
            r.emplace_back("installed", Value(true));
            if (const Value* d = reg.Find("dir"); d && d->IsString()) r.emplace_back("dir", *d);
            if (const Value* v = reg.Find("version"); v && v->IsString()) r.emplace_back("version", *v);
            if (const Value* m = reg.Find("mode"); m && m->IsString()) r.emplace_back("mode", *m);
            if (const Value* n = reg.Find("appName"); n && n->IsString()) r.emplace_back("app", *n);
        } else {
            r.emplace_back("installed", Value(false));
        }
        Ok(res, Value(std::move(r)));
        return;
    }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto parsed = ow::json::Parse(raw);
    Value man = parsed.value ? *parsed.value : Value(nullptr);
    Object r;
    r.emplace_back("installed", Value(true));
    if (!dir.empty()) r.emplace_back("dir", Value(dir));
    if (man.IsObject()) {
        if (const Value* v = man.Find("version"); v && v->IsString()) r.emplace_back("version", *v);
        if (const Value* v = man.Find("mode"); v && v->IsString()) r.emplace_back("mode", *v);
        if (const Value* v = man.Find("app"); v && v->IsString()) r.emplace_back("app", *v);
    }
    Ok(res, Value(std::move(r)));
}

/// Directorio de instalación por defecto (absoluto, por usuario).
static void defaultDir(const ow_request_t*, ow_response_t* res) {
#ifdef _WIN32
    const char* la = std::getenv("LOCALAPPDATA");
    const std::string base = (la && *la) ? std::string(la) : ExpandPath("~");
    Ok(res, Value(base + "\\" + AppSlug()));
#else
    // Linux/macOS per-usuario: ~/.local/opt/<slug>
    Ok(res, Value(ExpandPath("~/.local/opt/" + AppSlug())));
#endif
}

/// Selector nativo de carpeta (no necesita módulos .owm).
static void chooseDir(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    const std::string dir = platform::ChooseDir(
        Str(a, "title", "Elegir carpeta"), ExpandPath(Str(a, "defaultPath")));
    if (dir.empty()) {
        RespondOk(res, "null");
        return;
    }
    Ok(res, Value(dir));
}

static void shortcuts(const ow_request_t* req, ow_response_t* res) {
    const Value a = Args(req);
    std::string iconPath = ExpandPath(Str(a, "iconPath"));
    if (iconPath.empty()) {
        // Default: el icono embebido junto al payload (installer.json → "icon").
        const auto& m = Meta();
        if (m.valid) {
            if (const Value* ic = m.root.Find("icon"); ic && ic->IsString()) {
                const std::string candidate = PayloadRoot() + "/" + ic->AsString();
                std::error_code ec;
                if (fs::is_regular_file(candidate, ec)) iconPath = candidate;
            }
        }
    }
    const bool ok = platform::CreateShortcuts(
        Str(a, "appId", AppId()), Str(a, "appName", AppName()),
        ExpandPath(Str(a, "execPath")), iconPath,
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
        {"defaultDir", &installermod::defaultDir},
        {"chooseDir", &installermod::chooseDir},
        {"plan", &installermod::plan},
        {"install", &installermod::install},
        {"uninstall", &installermod::uninstall},
        {"verify", &installermod::verify},
        {"state", &installermod::state},
        {"list", &installermod::list},
        {"shortcuts", &installermod::shortcuts},
        {"launch", &installermod::launch},
        {"elevate", &installermod::elevate},
    };
    static const ow_module_desc_t d{"installer", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
} // namespace ow::internal
