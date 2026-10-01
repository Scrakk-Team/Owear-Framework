// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/ControlServer.hpp — canal de control kernel ↔ SDK JS.
//
// Protocolo (NDJSON, una línea por mensaje):
//   request:  {"id":N,"cmd":"window.create","params":{...}}
//   response: {"id":N,"ok":true,"result":{...}} | {"id":N,"ok":false,"error":"..."}
//   event:    {"event":"window.event","params":{"windowId":N,"name":"resize","payload":null}}
//
// El transporte (UDS / named pipe) vive en ControlServer_<platform>.cpp.
//
#pragma once

#include "ow/Common.h"
#include "ow/detail/minjson.hpp"

#include <map>
#include <string>

namespace ow {

class Window;
std::map<WindowId, Window*>& LiveWindows();

class ControlServer {
public:
    /// Singleton de plataforma (definido en ControlServer_<plat>.cpp).
    static ControlServer& Get();

    bool Start();   // bind + listen + integración con el main loop
    void Stop();

    std::string SocketPath() const;

    /// Registra + cablea una ventana recién creada (cualquier vía: comando
    /// window.create o creación directa del kernel como OW_DEMO), para que
    /// también reciba eventos de módulos.
    static void RegisterWindow(Window* w);

    /// Evento hacia todos los clientes conectados (SDK JS).
    void BroadcastEvent(const std::string& name, std::string_view paramsJson);

    /// Renderer → main (puente Node): reenvía `node.call` al proceso principal
    /// por el socket y guarda reqId → (ventana, invokeId). El main responde con
    /// `node.respond` (o empuja eventos con `node.emit`). Es la vía para que el
    /// renderer use Node (p. ej. el extension host), sin IPC por defecto.
    void ForwardNodeCall(WindowId windowId, uint64_t invokeId,
                         const std::string& fn, std::string_view argsJson);

    // ── transporte (plataforma) ────────────────────────────────────
    /// Crea el endpoint escuchando. Devuelve false si el SO lo impide.
    virtual bool PlatformListen() = 0;
    /// Envía una línea NDJSON a un cliente (o broadcast si clientId == 0).
    virtual void PlatformSend(uint64_t clientId, std::string_view line) = 0;
    virtual void PlatformStop() = 0;

    /// El transporte llama esto por cada línea completa recibida.
    void HandleLine(uint64_t clientId, std::string_view line);
    /// El transporte llama al aceptar/descartar clientes.
    void HandleClientDisconnected(uint64_t clientId);

protected:
    std::string socketPath_;
    bool started_ = false;

    void SendResponse(uint64_t clientId, uint64_t id, bool ok,
                      std::string_view resultJson, std::string_view error = {});
    void SendLine(uint64_t clientId, std::string_view line);

private:
    bool HandleCommand(uint64_t clientId, uint64_t id, const std::string& cmd,
                       std::string_view paramsJson, std::string& resultJson,
                       std::string& error);
    void WireWindowEvents(WindowId id, Window* w);

    // ── comandos por area (src/Control/Commands/*.cpp) ──────────────────
    bool CmdWindow(const std::string& cmd, const json::Value& params,
               uint64_t clientId, uint64_t id, std::string& resultJson,
               std::string& error);
    // Sub-dispatchers de window.* (src/Control/Commands/Window/*.cpp).
    bool CmdWindowCreate(const json::Value& params, std::string& resultJson);
    bool CmdWindowState(const std::string& cmd, const json::Value& params,
                        Window* w, std::string& resultJson, std::string& error);
    bool CmdWindowPage(const std::string& cmd, const json::Value& params,
                       uint64_t clientId, uint64_t id, Window* w,
                       std::string& resultJson, std::string& error);
    bool CmdWindowCharter(const std::string& cmd, const json::Value& params,
                          Window* w, std::string& resultJson, std::string& error);
    bool CmdMenu(const std::string& cmd, const json::Value& params,
               uint64_t clientId, uint64_t id, std::string& resultJson,
               std::string& error);
    bool CmdProtocol(const std::string& cmd, const json::Value& params,
               uint64_t clientId, uint64_t id, std::string& resultJson,
               std::string& error);
    bool CmdSession(const std::string& cmd, const json::Value& params,
               uint64_t clientId, uint64_t id, std::string& resultJson,
               std::string& error);
    bool CmdWebContents(const std::string& cmd, const json::Value& params,
               uint64_t clientId, uint64_t id, std::string& resultJson,
               std::string& error);
    bool CmdWebRequest(const std::string& cmd, const json::Value& params,
               uint64_t clientId, uint64_t id, std::string& resultJson,
               std::string& error);
    bool CmdNodeBridge(const std::string& cmd, const json::Value& params,
               uint64_t clientId, uint64_t id, std::string& resultJson,
               std::string& error);
    bool CmdApp(const std::string& cmd, const json::Value& params,
                std::string& resultJson, std::string& error);
    bool CmdNode(const std::string& cmd, const json::Value& params,
                 std::string& resultJson, std::string& error);
    bool CmdModule(const std::string& cmd, const json::Value& params,
                   std::string& resultJson, std::string& error);

    /// node.call pendientes de respuesta del main: reqId → (ventana, invokeId).
    struct PendingNodeCall {
        WindowId windowId;
        uint64_t invokeId;
    };
    std::map<uint64_t, PendingNodeCall> pendingNodeCalls_;
    uint64_t nextNodeReqId_ = 1;
};

} // namespace ow
