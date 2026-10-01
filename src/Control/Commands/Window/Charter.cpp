// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/Window/Charter.cpp — `window.*Charter` commands.
//
// The charter is the capability policy of a window's own document. These
// commands are the wire between @owear/core (win.setCharter) and the kernel
// enforcement point (src/Session/Charter.cpp).
#include "../../ControlServer.hpp"
#include "../Util.hpp"
#include "../../../Session/Charter.hpp"
#include "../../../Window/Window_p.hpp"
#include "ow/Window.h"

namespace ow {

using V = json::Value;

namespace {

std::vector<std::string> StringList(const V* v) {
    std::vector<std::string> out;
    if (!v) return out;
    if (v->IsString()) {
        out.push_back(v->AsString());
        return out;
    }
    if (!v->IsArray()) return out;
    for (const auto& item : v->AsArray())
        if (item.IsString()) out.push_back(item.AsString());
    return out;
}

std::string SpecJson(const Charter::Spec& spec) {
    json::Array allow;
    for (const auto& a : spec.allow) allow.emplace_back(V(a));
    json::Array deny;
    for (const auto& d : spec.deny) deny.emplace_back(V(d));
    json::Object o;
    o.emplace_back("enforce", V(spec.enforce));
    o.emplace_back("allow", V(std::move(allow)));
    o.emplace_back("deny", V(std::move(deny)));
    return V(std::move(o)).Serialize();
}

} // namespace

bool ControlServer::CmdWindowCharter(const std::string& cmd,
                                     const json::Value& params, Window* w,
                                     std::string& resultJson,
                                     std::string& error) {
    (void)error;
    const uint32_t wid = static_cast<uint32_t>(w->Id());

    if (cmd == "window.setCharter") {
        Charter::Spec spec;
        const V* allow = params.Find("allow");
        const V* deny = params.Find("deny");
        spec.allow = StringList(allow);
        spec.deny = StringList(deny);
        // A charter with grants is enforced by default; pass enforce=false to
        // keep the rules stored but paused (handy in dev).
        const V* enforce = params.Find("enforce");
        spec.enforce = (enforce && enforce->IsBool())
                           ? enforce->AsBool()
                           : (!spec.allow.empty() || !spec.deny.empty());
        Charter::Get().Set(wid, spec);
        resultJson = SpecJson(Charter::Get().Get(wid));
        return true;
    }
    if (cmd == "window.getCharter") {
        resultJson = SpecJson(Charter::Get().Get(wid));
        return true;
    }
    if (cmd == "window.clearCharter") {
        Charter::Get().Clear(wid);
        resultJson = SpecJson(Charter::Get().Get(wid));
        return true;
    }
    return false;
}

} // namespace ow
