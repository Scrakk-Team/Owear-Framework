// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/Window/State.cpp — estado/estilo/geometria de ventana.
#include "../../ControlServer.hpp"
#include "../Util.hpp"
#include "../../../Bridge/Dispatcher.hpp"
#include "../../../Protocol/ProtocolRegistry.hpp"
#include "../../../Session/PermissionBroker.hpp"
#include "../../../Session/WebRequestBroker.hpp"
#include "../../../Session/WindowOpenBroker.hpp"
#include "ow/Module.h"
#include "ow/Common.h"
#include "../../../Window/Window_p.hpp"
#include "ow/Window.h"
#include "ow/Base64.h"
#include "ow/Shm.h"

namespace ow {

using V = json::Value;

bool ControlServer::CmdWindowState(const std::string& cmd,
                                   const json::Value& params, Window* w,
                                   std::string& resultJson, std::string& error) {
    // ── C9: estado/estilo de ventana ─────────────────────────────────────
    auto boolResult = [&](bool b) { resultJson = b ? "true" : "false"; };
    auto sizeResult = [&](int width, int height) {
        json::Object o;
        o.emplace_back("width", V(static_cast<int64_t>(width)));
        o.emplace_back("height", V(static_cast<int64_t>(height)));
        resultJson = V(std::move(o)).Serialize();
    };
    auto rectResult = [&](const Window::Bounds& b) {
        json::Object o;
        o.emplace_back("x", V(static_cast<int64_t>(b.x)));
        o.emplace_back("y", V(static_cast<int64_t>(b.y)));
        o.emplace_back("width", V(static_cast<int64_t>(b.w)));
        o.emplace_back("height", V(static_cast<int64_t>(b.h)));
        resultJson = V(std::move(o)).Serialize();
    };
    auto boolArg = [&](const char* k, bool dflt) {
        const V* v = params.Find(k);
        return (v && v->IsBool()) ? v->AsBool() : dflt;
    };
    auto numArg = [&](const char* k, int dflt) {
        const V* v = params.Find(k);
        return (v && v->IsNumber()) ? static_cast<int>(v->AsInt()) : dflt;
    };

    if (cmd == "window.isVisible") { boolResult(w->IsVisible()); return true; }
    if (cmd == "window.isFocused") { boolResult(w->IsFocused()); return true; }
    if (cmd == "window.isResizable") { boolResult(w->IsResizable()); return true; }
    if (cmd == "window.isMovable") { boolResult(w->IsMovable()); return true; }
    if (cmd == "window.isMinimizable") { boolResult(w->IsMinimizable()); return true; }
    if (cmd == "window.isMaximizable") { boolResult(w->IsMaximizable()); return true; }
    if (cmd == "window.isClosable") { boolResult(w->IsClosable()); return true; }
    if (cmd == "window.isAlwaysOnTop") { boolResult(w->IsAlwaysOnTop()); return true; }
    if (cmd == "window.isKiosk") { boolResult(w->IsKiosk()); return true; }
    if (cmd == "window.isDestroyed") { boolResult(w->IsDestroyed()); return true; }
    if (cmd == "window.isFullScreen") { boolResult(w->IsFullScreen()); return true; }
    if (cmd == "window.setResizable") { w->SetResizable(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setMovable") { w->SetMovable(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setMinimizable") { w->SetMinimizable(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setMaximizable") { w->SetMaximizable(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setClosable") { w->SetClosable(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setAlwaysOnTop") {
        w->SetAlwaysOnTop(boolArg("on", true), numArg("level", 0));
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setSkipTaskbar") { w->SetSkipTaskbar(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setHasShadow") { w->SetHasShadow(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setKiosk") { w->SetKiosk(boolArg("on", true)); resultJson = "null"; return true; }
    if (cmd == "window.setIgnoreMouseEvents") {
        w->SetIgnoreMouseEvents(boolArg("ignore", true), boolArg("forward", false));
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setProgressBar") {
        const V* v = params.Find("value");
        std::string mode = "normal";
        if (const V* m = params.Find("mode"); m && m->IsString()) mode = m->AsString();
        w->SetProgressBar(v && v->IsNumber() ? v->AsDouble() : 0.0, mode);
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setBackgroundColor") {
        const V* c = params.Find("color");
        w->SetBackgroundColor(c && c->IsString() ? c->AsString() : std::string());
        resultJson = "null";
        return true;
    }
    if (cmd == "window.moveTop") { w->MoveTop(); resultJson = "null"; return true; }
    if (cmd == "window.setAspectRatio") {
        const V* r = params.Find("ratio");
        w->SetAspectRatio(r && r->IsNumber() ? r->AsDouble() : 0.0, numArg("extraW", 0),
                          numArg("extraH", 0));
        resultJson = "null";
        return true;
    }
    if (cmd == "window.getContentBounds") { rectResult(w->GetContentBounds()); return true; }
    if (cmd == "window.setContentSize") {
        w->SetContentSize(numArg("width", 0), numArg("height", 0));
        resultJson = "null";
        return true;
    }
    if (cmd == "window.getContentSize") {
        auto s = w->GetContentSize();
        sizeResult(s.width, s.height);
        return true;
    }
    if (cmd == "window.getMinimumSize") {
        auto s = w->GetMinimumSize();
        sizeResult(s.width, s.height);
        return true;
    }
    if (cmd == "window.getMaximumSize") {
        auto s = w->GetMaximumSize();
        sizeResult(s.width, s.height);
        return true;
    }
    if (cmd == "window.setMinimumSize") {
        w->SetMinimumSize(numArg("width", 0), numArg("height", 0));
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setMaximumSize") {
        w->SetMaximumSize(numArg("width", 0), numArg("height", 0));
        resultJson = "null";
        return true;
    }
    if (cmd == "window.minimize") { w->Minimize(); resultJson = "null"; return true; }
    if (cmd == "window.maximize") {
        bool target = true;
        if (const V* v = params.Find("enabled"); v && v->IsBool()) target = v->AsBool();
        target ? w->Maximize() : w->Unmaximize();
        resultJson = "null";
        return true;
    }
    if (cmd == "window.unmaximize") { w->Unmaximize(); resultJson = "null"; return true; }
    if (cmd == "window.setFullScreen") {
        bool enabled = false;
        if (const V* v = params.Find("enabled"); v && v->IsBool()) enabled = v->AsBool();
        w->SetFullScreen(enabled);
        resultJson = "null";
        return true;
    }
    if (cmd == "window.isMaximized") {
        resultJson = w->IsMaximized() ? "true" : "false";
        return true;
    }
    if (cmd == "window.isMinimized") {
        resultJson = w->IsMinimized() ? "true" : "false";
        return true;
    }
    if (cmd == "window.getBounds") {
        auto b = w->GetBounds();
        json::Object o;
        o.emplace_back("x", V(b.x));
        o.emplace_back("y", V(b.y));
        o.emplace_back("width", V(b.w));
        o.emplace_back("height", V(b.h));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }
    if (cmd == "window.setBounds") {
        Window::Bounds b{};
        if (const V* v = params.Find("x"); v && v->IsNumber()) b.x = (int)v->AsInt();
        if (const V* v = params.Find("y"); v && v->IsNumber()) b.y = (int)v->AsInt();
        if (const V* v = params.Find("width"); v && v->IsNumber()) b.w = (int)v->AsInt();
        if (const V* v = params.Find("height"); v && v->IsNumber()) b.h = (int)v->AsInt();
        w->SetBounds(b);
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setTitleBarOverlay") {
        TitleBarOverlay ov;
        if (const V* v = params.Find("titleBarOverlay")) {
            if (v->IsBool()) {
                ov.enabled = v->AsBool();
            } else if (v->IsObject()) {
                ov.enabled = true;
                if (const V* e = v->Find("enabled"); e && e->IsBool())
                    ov.enabled = e->AsBool();
                if (const V* c = v->Find("color"); c && c->IsString())
                    ov.color = c->AsString();
                if (const V* sc = v->Find("symbolColor"); sc && sc->IsString())
                    ov.symbolColor = sc->AsString();
                if (const V* bc = v->Find("buttonColor"); bc && bc->IsString())
                    ov.buttonColor = bc->AsString();
                if (const V* h = v->Find("height"); h && h->IsNumber())
                    ov.height = static_cast<int>(h->AsInt());
            }
        } else {
            ov.enabled = true; // sin payload ⇒ activar
        }
        w->SetTitleBarOverlay(ov);
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setTitle") {
        const V* t = params.Find("title");
        if (!t || !t->IsString()) { error = "title requerido"; return false; }
        w->SetTitle(t->AsString());
        resultJson = "null";
        return true;
    }
    if (cmd == "window.loadURL") {
        const V* u = params.Find("url");
        if (!u || !u->IsString()) { error = "url requerida"; return false; }
        w->LoadURL(u->AsString());
        resultJson = "null";
        return true;
    }
    if (cmd == "window.respondCloseRequest") {
        const V* rid = params.Find("requestId");
        const V* allow = params.Find("allow");
        if (!rid || !rid->IsNumber() || !allow || !allow->IsBool()) {
            error = "requestId y allow requeridos";
            return false;
        }
        w->impl()->RespondJsClose(static_cast<uint64_t>(rid->AsInt()),
                                  allow->AsBool());
        resultJson = "null";
        return true;
    }
    return false;
}

} // namespace ow
