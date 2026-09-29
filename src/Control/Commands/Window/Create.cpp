// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/Window/Create.cpp — creacion de ventana (window.create).
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

bool ControlServer::CmdWindowCreate(const json::Value& params,
                                     std::string& resultJson) {
        WindowOptions opts;
        if (const V* t = params.Find("title"); t && t->IsString()) opts.title = t->AsString();
        if (const V* v = params.Find("width"); v && v->IsNumber()) opts.width = (int)v->AsInt();
        if (const V* v = params.Find("height"); v && v->IsNumber()) opts.height = (int)v->AsInt();
        if (const V* v = params.Find("x"); v && v->IsNumber()) opts.center = false;
        if (const V* v = params.Find("resizable"); v && v->IsBool()) opts.resizable = v->AsBool();
        if (const V* v = params.Find("frameless"); v && v->IsBool()) opts.frameless = v->AsBool();
        if (opts.frameless) opts.titleBarStyle = TitleBarStyle::Hidden;
        if (const V* v = params.Find("titleBarStyle"); v && v->IsString()) {
            std::string s = v->AsString();
            if (s == "hidden") opts.titleBarStyle = TitleBarStyle::Hidden;
            else if (s == "custom") opts.titleBarStyle = TitleBarStyle::Custom;
            else opts.titleBarStyle = TitleBarStyle::Default;
        }
        if (const V* v = params.Find("titleBarOverlay")) {
            TitleBarOverlay ov;
            if (v->IsBool()) {
                ov.enabled = v->AsBool();
            } else if (v->IsObject()) {
                ov.enabled = true; // presente ⇒ activo (estilo Electron)
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
            opts.titleBarOverlay = ov;
        }
        // titleBarOverlay implica titlebar custom (como Electron con hidden).
        if (opts.titleBarOverlay.enabled &&
            opts.titleBarStyle == TitleBarStyle::Default)
            opts.titleBarStyle = TitleBarStyle::Custom;
        if (const V* v = params.Find("url"); v && v->IsString()) opts.url = v->AsString();
        if (const V* v = params.Find("session"); v && v->IsString()) opts.session = v->AsString();
        // C9: opciones de ventana
        if (const V* v = params.Find("show"); v && v->IsBool()) opts.show = v->AsBool();
        if (const V* v = params.Find("parent"); v && v->IsNumber())
            opts.parent = static_cast<WindowId>(v->AsInt());
        if (const V* v = params.Find("modal"); v && v->IsBool()) opts.modal = v->AsBool();
        if (const V* v = params.Find("transparent"); v && v->IsBool())
            opts.transparent = v->AsBool();
        if (const V* v = params.Find("backgroundColor"); v && v->IsString())
            opts.backgroundColor = v->AsString();
        if (const V* v = params.Find("movable"); v && v->IsBool()) opts.movable = v->AsBool();
        if (const V* v = params.Find("minimizable"); v && v->IsBool())
            opts.minimizable = v->AsBool();
        if (const V* v = params.Find("maximizable"); v && v->IsBool())
            opts.maximizable = v->AsBool();
        if (const V* v = params.Find("closable"); v && v->IsBool()) opts.closable = v->AsBool();
        if (const V* v = params.Find("fullscreenable"); v && v->IsBool())
            opts.fullscreenable = v->AsBool();
        if (const V* v = params.Find("skipTaskbar"); v && v->IsBool())
            opts.skipTaskbar = v->AsBool();
        if (const V* v = params.Find("alwaysOnTop"); v && v->IsBool())
            opts.alwaysOnTop = v->AsBool();
        if (const V* v = params.Find("hasShadow"); v && v->IsBool())
            opts.hasShadow = v->AsBool();
        if (const V* v = params.Find("minWidth"); v && v->IsNumber())
            opts.minWidth = static_cast<int>(v->AsInt());
        if (const V* v = params.Find("minHeight"); v && v->IsNumber())
            opts.minHeight = static_cast<int>(v->AsInt());
        if (const V* v = params.Find("maxWidth"); v && v->IsNumber())
            opts.maxWidth = static_cast<int>(v->AsInt());
        if (const V* v = params.Find("maxHeight"); v && v->IsNumber())
            opts.maxHeight = static_cast<int>(v->AsInt());
        if (const V* v = params.Find("aspectRatio"); v && v->IsNumber())
            opts.aspectRatio = v->AsDouble();

        auto* win = new Window(opts); // se auto-registra en LiveWindows
        WindowId wid = win->Id();

        json::Object o;
        o.emplace_back("windowId", V(static_cast<int64_t>(wid)));
        resultJson = V(std::move(o)).Serialize();
        return true;
}

} // namespace ow
