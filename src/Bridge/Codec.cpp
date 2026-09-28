// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#include "ow/Bridge/Codec.h"

#include "ow/Base64.h"
#include "ow/detail/minjson.hpp"

namespace ow::bridge {

namespace {

struct Span {
    size_t b = 0, e = 0;
};

void SkipWs(std::string_view s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

/// Lee un string JSON (s[i]=='"') y devuelve su contenido desescapado.
bool ReadString(std::string_view s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c != '\\') {
            out += c;
            continue;
        }
        if (i >= s.size()) return false;
        char e = s[i++];
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':
                // claves/valores cortos: conservamos el escape tal cual
                if (i + 4 > s.size()) return false;
                out += "\\u";
                out.append(s.substr(i, 4));
                i += 4;
                break;
            default: out += e; break; // \" \\ \/
        }
    }
    return false;
}

/// Salta un valor JSON completo desde i (sin construirlo).
bool SkipValue(std::string_view s, size_t& i) {
    SkipWs(s, i);
    if (i >= s.size()) return false;
    const char c = s[i];
    if (c == '"') {
        std::string tmp;
        return ReadString(s, i, tmp);
    }
    if (c == '{' || c == '[') {
        const char open = c, close = (c == '{') ? '}' : ']';
        int depth = 0;
        while (i < s.size()) {
            const char d = s[i];
            if (d == '"') {
                std::string tmp;
                if (!ReadString(s, i, tmp)) return false;
                continue;
            }
            if (d == open) ++depth;
            else if (d == close) {
                --depth;
                if (depth == 0) {
                    ++i;
                    return true;
                }
            }
            ++i;
        }
        return false;
    }
    while (i < s.size()) {
        const char d = s[i];
        if (d == ',' || d == '}' || d == ']' || d == ' ' || d == '\t' || d == '\n' ||
            d == '\r')
            break;
        ++i;
    }
    return true;
}

/// Escanea el objeto raíz: clave → span del valor (crudo, sin construirlo).
bool ScanTop(std::string_view s, std::vector<std::pair<std::string, Span>>& out) {
    size_t i = 0;
    SkipWs(s, i);
    if (i >= s.size() || s[i] != '{') return false;
    ++i;
    for (;;) {
        SkipWs(s, i);
        if (i < s.size() && s[i] == '}') return true;
        if (i >= s.size() || s[i] != '"') return false;
        std::string key;
        if (!ReadString(s, i, key)) return false;
        SkipWs(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        SkipWs(s, i);
        const size_t vb = i;
        if (!SkipValue(s, i)) return false;
        out.emplace_back(std::move(key), Span{vb, i});
        SkipWs(s, i);
        if (i < s.size() && s[i] == ',') {
            ++i;
            continue;
        }
        if (i < s.size() && s[i] == '}') return true;
        return false;
    }
}

bool StrOf(std::string_view s, const Span* sp, std::string& out) {
    if (!sp) return false;
    size_t i = sp->b;
    return ReadString(s, i, out);
}

uint64_t NumOf(std::string_view s, const Span* sp) {
    if (!sp) return 0;
    uint64_t v = 0;
    for (size_t i = sp->b; i < sp->e; ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') break;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    return v;
}

} // namespace

bool DecodeMessageSlow(std::string_view text, Message& out);

bool DecodeMessage(std::string_view text, Message& out) {
    out.json.clear();
    out.bin.clear();
    out.module.clear();
    out.method.clear();
    out.name.clear();

    std::vector<std::pair<std::string, Span>> members;
    if (!ScanTop(text, members)) return DecodeMessageSlow(text, out); // fallback
    auto find = [&](std::string_view k) -> const Span* {
        for (auto& m : members)
            if (m.first == k) return &m.second;
        return nullptr;
    };
    auto raw = [&](const Span* sp) -> std::string {
        return sp ? std::string(text.substr(sp->b, sp->e - sp->b)) : std::string();
    };

    std::string type;
    StrOf(text, find("t"), type);
    if (type.empty()) return false;
    out.to = static_cast<WindowId>(NumOf(text, find("to")));

    if (type == "event") {
        out.type = MsgType::Event;
        out.window = static_cast<WindowId>(NumOf(text, find("w")));
        if (!StrOf(text, find("n"), out.name)) return false;
        out.json = raw(find("p"));
        if (out.json.empty()) out.json = "null";
        return !out.name.empty();
    }

    if (type == "invoke") {
        out.type = MsgType::Invoke;
        out.id = NumOf(text, find("id"));
        out.window = static_cast<WindowId>(NumOf(text, find("w")));
        if (!StrOf(text, find("m"), out.module)) return false;
        if (!StrOf(text, find("f"), out.method)) return false;
        out.json = raw(find("a")); // el array tal cual: el módulo lo parsea UNA vez
        if (out.json.empty()) out.json = "[]";
        if (const Span* b = find("b")) {
            std::string b64;
            if (StrOf(text, b, b64) && !b64.empty()) {
                std::vector<uint8_t> decoded;
                if (ow::b64::Decode(b64, decoded)) out.bin = std::move(decoded);
            }
        }
        return !out.module.empty() && !out.method.empty();
    }

    return false;
}

// Implementación antigua (parser completo) conservada como referencia/fallback.
bool DecodeMessageSlow(std::string_view text, Message& out) {
    auto parsed = json::Parse(text);
    if (!parsed.value || !parsed.value->IsObject()) return false;
    const auto& obj = parsed.value->AsObject();

    const json::Value* t = nullptr;
    for (const auto& m : obj) {
        if (m.first == "t") t = &m.second;
    }
    if (!t || !t->IsString()) return false;
    std::string_view type = t->AsString();

    auto getStr = [&](std::string_view key, std::string& dst) {
        if (const json::Value* v = parsed.value->Find(key); v && v->IsString())
            dst = v->AsString();
    };
    auto getU64 = [&](std::string_view key, uint64_t& dst) {
        if (const json::Value* v = parsed.value->Find(key); v && v->IsNumber())
            dst = static_cast<uint64_t>(v->AsInt());
    };

    out.json.clear();
    out.bin.clear();
    out.module.clear();
    out.method.clear();
    out.name.clear();

    uint64_t to = 0;
    getU64("to", to);
    out.to = static_cast<WindowId>(to);

    if (type == "event") {
        out.type = MsgType::Event;
        uint64_t w = 0;
        getU64("w", w);
        out.window = static_cast<WindowId>(w);
        getStr("n", out.name);
        if (const json::Value* p = parsed.value->Find("p"); p)
            out.json = p->Serialize();
        else
            out.json = "null";
        return !out.name.empty();
    }

    if (type == "invoke") {
        out.type = MsgType::Invoke;
        uint64_t id = 0;
        uint64_t window = 0;
        getU64("id", id);
        getU64("w", window);
        out.id = id;
        out.window = static_cast<WindowId>(window);
        getStr("m", out.module);
        getStr("f", out.method);
        // args: serializa el array tal cual; default "[]"
        if (const json::Value* a = parsed.value->Find("a"); a)
            out.json = a->Serialize();
        else
            out.json = "[]";
        // binario opcional embebido en base64
        if (const json::Value* b = parsed.value->Find("b"); b && b->IsString()) {
            std::vector<uint8_t> decoded;
            if (ow::b64::Decode(b->AsString(), decoded)) out.bin = std::move(decoded);
        }
        return !out.module.empty() && !out.method.empty();
    }

    return false;
}

std::string EncodeInvokeResult(uint64_t id, bool ok,
                               std::string_view resultJson,
                               const uint8_t* bin, size_t binLen) {
    std::string out = "{\"t\":\"result\",\"id\":";
    out += std::to_string(id);
    out += ",\"ok\":";
    out += ok ? "true" : "false";
    out += ",\"r\":";
    out.append(resultJson.empty() ? "null" : resultJson);
    if (bin && binLen > 0) {
        out += ",\"b\":\"";
        out += ow::b64::Encode(bin, binLen);
        out += '"';
    }
    out += '}';
    return out;
}

std::string EncodeEvent(WindowId window, std::string_view name,
                        std::string_view jsonPayload) {
    std::string out = "{\"t\":\"event\",\"w\":";
    out += std::to_string(window);
    out += ",\"n\":";
    out += json::Value(std::string(name)).Serialize();
    out += ",\"p\":";
    out.append(jsonPayload.empty() ? "null" : jsonPayload);
    out += '}';
    return out;
}

} // namespace ow::bridge
