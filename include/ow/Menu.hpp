// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/menu/src/menu_template.hpp — parser común del template de menú.
//
// Formato (igual que el SDK): { id?, label?, role?, type?, checked?, enabled?,
// visible?, accelerator?, icon?(base64 png), submenu?: [...] }
//
#pragma once

#include "ow/Json.h"

#include <string>
#include <vector>

namespace ow::menu {

using ow::json::Value;

struct Item {
    std::string id;
    std::string label;
    std::string role;
    std::string type = "normal"; // normal|separator|checkbox|radio
    bool checked = false;
    bool enabled = true;
    bool visible = true;
    std::string accelerator;
    std::string icon; // PNG en base64 (opcional)
    std::vector<Item> submenu;
};

inline Item ParseItem(const Value& v) {
    Item it;
    auto str = [&](const char* k, std::string& out) {
        if (const Value* p = v.Find(k); p && p->IsString()) out = p->AsString();
    };
    auto boolean = [&](const char* k, bool& out) {
        if (const Value* p = v.Find(k); p && p->IsBool()) out = p->AsBool();
    };
    str("id", it.id);
    str("label", it.label);
    str("role", it.role);
    str("type", it.type);
    str("accelerator", it.accelerator);
    str("icon", it.icon);
    boolean("checked", it.checked);
    boolean("enabled", it.enabled);
    boolean("visible", it.visible);
    if (const Value* sub = v.Find("submenu"); sub && sub->IsArray())
        for (const auto& s : sub->AsArray()) it.submenu.push_back(ParseItem(s));
    return it;
}

inline std::vector<Item> ParseItems(const Value& arr) {
    std::vector<Item> out;
    if (arr.IsArray())
        for (const auto& v : arr.AsArray()) out.push_back(ParseItem(v));
    return out;
}

/// Payload de click: {"id":..,"role":..,"windowId":N}
inline std::string ClickPayload(const Item& it, uint32_t windowId) {
    std::string j = "{\"id\":";
    j += ow::json::Value(it.id).Serialize();
    j += ",\"role\":";
    j += ow::json::Value(it.role).Serialize();
    j += ",\"windowId\":";
    j += std::to_string(windowId);
    j += "}";
    return j;
}

} // namespace ow::menu
