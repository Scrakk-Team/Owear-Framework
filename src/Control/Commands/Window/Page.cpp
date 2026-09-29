// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Commands/Window/Page.cpp — pagina: eval/capture/printToPDF.
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

bool ControlServer::CmdWindowPage(const std::string& cmd,
                                  const json::Value& params, uint64_t clientId,
                                  uint64_t id, Window* w, std::string& resultJson,
                                  std::string& error) {
    (void)error;
    // ── theme: forzar el esquema de color del contenido ──────────────────

    // ── menu: menubar de aplicación ──────────────────────────────────────
    if (cmd == "window.eval") {
        const V* js = params.Find("js");
        if (!js || !js->IsString()) { error = "js requerido"; return false; }
        // Respuesta asíncrona: se envía con el mismo id cuando llegue el callback
        w->EvalJS(js->AsString(), [this, clientId, id](std::string_view result) {
            bool ok = result.find("owError") == std::string::npos;
            SendResponse(clientId, id, ok, result, ok ? "" : "eval falló");
        });
        return true; // respuesta ya enviada (o pendiente) — evita doble send
    }

    if (cmd == "window.capturePage") {
        const V* b = params.Find("base64");
        const bool base64 = b && b->IsBool() && b->AsBool();
        // Respuesta asíncrona: el backend captura y responde al terminar
        // (sin pump anidado, que en Windows reentraba y crasheaba).
        w->CapturePage([this, clientId, id, base64](bool ok, const std::string& png) {
            if (!ok) {
                SendResponse(clientId, id, false, "null", "capture falló");
                return;
            }
            std::string result;
            if (base64) {
                json::Object o;
                o.emplace_back("data", V(ow::b64::Encode(png)));
                o.emplace_back("format", V("png"));
                result = V(std::move(o)).Serialize();
            } else {
                const char* sid = ow_shm_put(
                    reinterpret_cast<const uint8_t*>(png.data()), png.size());
                if (!sid || !*sid) {
                    SendResponse(clientId, id, false, "null", "SHM llena");
                    return;
                }
                json::Object shm;
                shm.emplace_back("id", V(std::string(sid)));
                shm.emplace_back("size", V(static_cast<int64_t>(png.size())));
                json::Object o;
                o.emplace_back("__ow_shm", V(std::move(shm)));
                o.emplace_back("format", V("png"));
                result = V(std::move(o)).Serialize();
            }
            SendResponse(clientId, id, true, result, "");
        });
        return true; // respuesta pendiente — evita doble send
    }

    if (cmd == "window.printToPDF") {
        // Respuesta asíncrona: el backend exporta y responde al terminar.
        w->PrintToPDF([this, clientId, id](bool ok, const std::string& pdf) {
            if (!ok) {
                SendResponse(clientId, id, false, "null", "printToPDF falló");
                return;
            }
            json::Object o;
            o.emplace_back("data", V(ow::b64::Encode(pdf)));
            o.emplace_back("format", V("pdf"));
            SendResponse(clientId, id, true, V(std::move(o)).Serialize(), "");
        });
        return true;
    }
    return false;
}

} // namespace ow
