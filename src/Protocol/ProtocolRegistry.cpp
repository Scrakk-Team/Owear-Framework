// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Protocol/ProtocolRegistry.cpp
//
#include "ProtocolRegistry.hpp"

#include "../Control/ControlServer.hpp"
#include "ow/Base64.h"
#include "ow/Json.h"

#include <fstream>
#include <sstream>

namespace ow {

namespace {

std::string GuessContentType(const std::string& path) {
    auto ends = [&](const char* ext) {
        const size_t n = std::char_traits<char>::length(ext);
        return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
    };
    if (ends(".html") || ends(".htm")) return "text/html";
    if (ends(".js") || ends(".mjs")) return "text/javascript";
    if (ends(".css")) return "text/css";
    if (ends(".json")) return "application/json";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".png")) return "image/png";
    if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
    if (ends(".wasm")) return "application/wasm";
    if (ends(".txt") || ends(".md")) return "text/plain";
    return "application/octet-stream";
}

} // namespace

ProtocolRegistry& ProtocolRegistry::Get() {
    static ProtocolRegistry instance;
    return instance;
}

void ProtocolRegistry::Register(const ProtocolScheme& scheme) {
    if (scheme.name.empty()) return;
    schemes_[scheme.name] = scheme;
}

bool ProtocolRegistry::Has(const std::string& name) const {
    return schemes_.count(name) != 0;
}

const ProtocolScheme* ProtocolRegistry::Find(const std::string& name) const {
    auto it = schemes_.find(name);
    return it == schemes_.end() ? nullptr : &it->second;
}

std::vector<ProtocolScheme> ProtocolRegistry::All() const {
    std::vector<ProtocolScheme> out;
    out.reserve(schemes_.size());
    for (const auto& [name, s] : schemes_) out.push_back(s);
    return out;
}

std::string ProtocolRegistry::UrlHost(const std::string& url) {
    const size_t sep = url.find("://");
    if (sep == std::string::npos) return {};
    const size_t start = sep + 3;
    const size_t end = url.find_first_of("/?#", start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string ProtocolRegistry::UrlPath(const std::string& url) {
    const size_t sep = url.find("://");
    if (sep == std::string::npos) return "/";
    const size_t start = url.find('/', sep + 3);
    if (start == std::string::npos) return "/";
    const size_t end = url.find_first_of("?#", start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void ProtocolRegistry::Dispatch(const std::string& scheme, const std::string& url,
                                const std::string& method, const std::string& headersJson,
                                const std::string& bodyBase64, ResolveCb cb) {
    const ProtocolScheme* s = Find(scheme);
    if (!s) {
        Response r;
        r.status = 404;
        r.resolved = false;
        r.error = "esquema no registrado: " + scheme;
        cb(std::move(r));
        return;
    }

    if (s->serveDir) {
        std::string rel = UrlPath(url);
        while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
        if (rel.empty()) rel = "index.html";
        const std::filesystem::path file = s->root / rel;
        std::ifstream in(file, std::ios::binary);
        Response r;
        if (!in) {
            r.status = 404;
            r.resolved = false;
            r.error = "no existe: " + file.string();
            cb(std::move(r));
            return;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        r.status = 200;
        r.headersJson = "{\"content-type\":\"" + GuessContentType(rel) + "\"}";
        r.body = ss.str();
        cb(std::move(r));
        return;
    }

    if (s->hasHandler) {
        const uint64_t reqId = nextReqId_++;
        pending_[reqId] = std::move(cb);
        json::Object p;
        p.emplace_back("reqId", json::Value(static_cast<int64_t>(reqId)));
        p.emplace_back("scheme", json::Value(scheme));
        p.emplace_back("url", json::Value(url));
        p.emplace_back("method", json::Value(method));
        auto headers = json::Parse(headersJson);
        p.emplace_back("headers", headers.value ? std::move(*headers.value)
                                                : json::Value(json::Object{}));
        auto body = json::Parse("null");
        p.emplace_back("body", json::Value(bodyBase64)); // base64 ("" si vacío)
        ControlServer::Get().BroadcastEvent("protocol.request",
                                            json::Value(std::move(p)).Serialize());
        return;
    }

    Response r;
    r.status = 404;
    r.resolved = false;
    r.error = "esquema sin handler ni directorio: " + scheme;
    cb(std::move(r));
}

void ProtocolRegistry::ResolveFromMain(uint64_t reqId, int status,
                                       const std::string& headersJson,
                                       const std::string& bodyBase64) {
    auto it = pending_.find(reqId);
    if (it == pending_.end()) return;
    ResolveCb cb = std::move(it->second);
    pending_.erase(it);

    std::string body;
    if (!bodyBase64.empty()) b64::Decode(bodyBase64, body);

    Response r;
    r.status = status;
    r.headersJson = headersJson.empty() ? "{}" : headersJson;
    r.body = std::move(body);
    cb(std::move(r));
}

} // namespace ow
