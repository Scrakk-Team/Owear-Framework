// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/ControlServer_win.cpp — transporte named pipe de Windows.
// Ruta: \\.\pipe\owear-<pid>
//
#include "ControlServer.hpp"
#include "Platform/win/Internal.hpp"
#include "../Core/Log.hpp"
#include "ow/App.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ow {

namespace {

using namespace control_win_detail;
class PipeServer final : public ControlServer {
public:
    bool PlatformListen() override {
        char name[256];
        std::snprintf(name, sizeof(name), "\\\\.\\pipe\\owear-%lu",
                      static_cast<unsigned long>(GetCurrentProcessId()));
        socketPath_ = name;

        // pipe duplex por mensaje; aceptamos un cliente a la vez (SDK único).
        // SIN FILE_FLAG_OVERLAPPED: todo el I/O de este hilo es bloqueante
        // (ConnectNamedPipe/ReadFile/WriteFile con OVERLAPPED=nullptr exige
        // handle síncrono; con overlapped+nullptr el comportamiento es indefinido).
        pipe_ = CreateNamedPipeA(
            name,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            4,   // instancias
            64 * 1024, 64 * 1024, 0, nullptr);
        if (pipe_ == INVALID_HANDLE_VALUE) return false;

        // hilo lector: acepta conexiones y reenvía líneas al main loop
        readerThread_ = std::thread([this] { ReaderLoop(); });
        return true;
    }

    void PlatformSend(uint64_t clientId, std::string_view line) override {
        // NO escribir aquí: este lo llama el hilo principal y el handle del
        // pipe está reservado al hilo lector. Encolamos la respuesta y el
        // hilo lector la drena en su siguiente pasada (sondeo con
        // PeekNamedPipe). NO se usa CancelIoEx: cancelaba también las
        // ESCRITURAS en vuelo (respuestas truncadas = protocolo desincronizado
        // y cuelgue bajo ráfagas) y, si el lector aún no estaba bloqueado,
        // dejaba la respuesta varada hasta el siguiente comando.
        {
            std::lock_guard lock(outboxMu_);
            if (clientId == 0) {
                for (auto& [id, c] : clients_)
                    outbox_.push_back({id, std::string(line) + '\n'});
            } else if (clients_.count(clientId)) {
                outbox_.push_back({clientId, std::string(line) + '\n'});
            }
        }
    }

    void PlatformStop() override {
        // orden crítico: primero despierta al lector (CancelIoEx), luego
        // espera a que SALGA de AcceptOne (ningún uso del handle en vuelo),
        // y SOLO entonces destruye el handle.
        running_ = false;
        if (pipe_ != INVALID_HANDLE_VALUE)
            CancelIoEx(pipe_, nullptr);
        if (readerThread_.joinable()) readerThread_.join();
        // respuestas encoladas sin entregar (p.ej. la de app.quit: el lector
        // dejó de drenar cuando running_ pasó a false). El handle ya es
        // exclusivo de este hilo — escritura directa segura.
        std::vector<std::pair<uint64_t, std::string>> pend;
        {
            std::lock_guard lock(outboxMu_);
            pend.swap(outbox_);
        }
        for (auto& [id, payload] : pend) {
            if (pipe_ == INVALID_HANDLE_VALUE) break;
            ClientBuf tmp{pipe_, {}};
            WriteClient(tmp, payload);
        }
        if (pipe_ != INVALID_HANDLE_VALUE) FlushFileBuffers(pipe_);
        if (pipe_ != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(pipe_);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    struct ClientBuf {
        HANDLE handle = INVALID_HANDLE_VALUE;
        std::string pending;
    };

    static void WriteClient(ClientBuf& c, std::string_view line) {
        if (c.handle == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        std::string out(line);
        out += '\n';
        if (!WriteFile(c.handle, out.data(), static_cast<DWORD>(out.size()),
                       &written, nullptr)) {
            log::Error("control", "WriteFile al cliente falló: " +
                                      std::to_string(GetLastError()));
        }
    }

    void RecreatePipe() {
        // SIN FILE_FLAG_FIRST_PIPE_INSTANCE: ese flag exige ser la primera
        // instancia del nombre y las recreaciones tras desconexión siempre
        // fallarían (mordido: solo el primer cliente podía conectar).
        pipe_ = CreateNamedPipeA(
            socketPath_.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            4, 64 * 1024, 64 * 1024, 0, nullptr);
        if (pipe_ == INVALID_HANDLE_VALUE) {
            log::Error("control", "RecreatePipe falló: " +
                                      std::to_string(GetLastError()));
        }
    }

    void AcceptOne() {
        if (!ConnectNamedPipe(pipe_, nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            // CancelIoEx (PlatformSend/Stop) aborta también la espera de
            // conexión: drena el outbox pendiente y reintenta si seguimos
            // vivos (sin esto, la respuesta de app.quit se pierde).
            if (running_) {
                DrainOutbox();
                if (pipe_ != INVALID_HANDLE_VALUE) return; // ReaderLoop reitera
            }
            return;
        }

        // ImpersonateNamedPipeClient exige que el cliente haya hecho I/O en
        // la pipe: leemos el primer chunk ANTES de verificar identidad
        // (gotcha Win32 — verificado en CI).
        char buf[8192];
        DWORD n = 0;
        if (!ReadFile(pipe_, buf, sizeof(buf), &n, nullptr) || n == 0) {
            DisconnectNamedPipe(pipe_);
            if (running_) RecreatePipe();
            return;
        }

        // Verificación mínima de seguridad: solo aceptamos clientes que
        // corran con el mismo usuario que este proceso. VERIFICAR-EN-WINDOWS.
        if (!ClientIsSameUser(pipe_)) {
            log::Warn("control", "conexión rechazada: usuario del pipe no coincide");
            DisconnectNamedPipe(pipe_);
            if (running_) RecreatePipe();
            return;
        }

        uint64_t id = nextClientId_++;
        {
            std::lock_guard lock(clientsMu_);
            clients_[id] = ClientBuf{pipe_, {}};
        }

        auto processChunk = [&](const char* data, DWORD len) {
            std::string acc;
            {
                std::lock_guard lock(clientsMu_);
                acc = std::move(clients_[id].pending);
            }
            acc.append(data, len);
            size_t pos;
            while ((pos = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, pos);
                acc.erase(0, pos + 1);
                // HWND/COM/WebView2 exigen tocar UI solo desde el hilo que los
                // creó (el hilo principal): HandleLine (y HandleCommand, que
                // manipula ventanas) se despacha al main loop. Este hilo
                // lector queda dedicado exclusivamente a I/O del pipe.
                App::Post([this, id, line] { HandleLine(id, line); });
            }
            std::lock_guard lock(clientsMu_);
            clients_[id].pending = std::move(acc);
        };
        processChunk(buf, n);

        // lee hasta desconexión (un cliente por instancia v1). Con el cliente
        // conectado el hilo lector alterna: vaciar el outbox (respuestas ya
        // listas) y sondear la pipe con PeekNamedPipe. No se usa CancelIoEx
        // desde PlatformSend: cancelaba también las escrituras en vuelo
        // (respuestas truncadas) y, si el lector aún no estaba bloqueado,
        // dejaba la respuesta varada. El sondeo drena en ~1 ms y garantiza
        // que nunca se escribe mientras hay un ReadFile pendiente (evita la
        // serialización de I/O del handle síncrono).
        while (running_) {
            DrainOutbox();
            DWORD avail = 0;
            if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr)) {
                break; // pipe rota / cliente desconectado
            }
            if (avail == 0) {
                Sleep(1); // sin datos: cede el hilo y reintenta
                continue;
            }
            const DWORD want = avail < sizeof(buf) ? avail
                                                   : static_cast<DWORD>(sizeof(buf));
            DWORD got = 0;
            if (!ReadFile(pipe_, buf, want, &got, nullptr) || got == 0) {
                if (GetLastError() == ERROR_OPERATION_ABORTED) {
                    if (!running_) break;
                    continue;
                }
                break;
            }
            processChunk(buf, got);
        }
        {
            std::lock_guard lock(clientsMu_);
            clients_.erase(id);
        }
        DisconnectNamedPipe(pipe_);
        // recrea instancia para el siguiente cliente
        if (running_) RecreatePipe();
    }

private:
    void DrainOutbox() {
        std::vector<std::pair<uint64_t, std::string>> batch;
        {
            std::lock_guard lock(outboxMu_);
            batch.swap(outbox_);
        }
        for (auto& [id, payload] : batch) {
            HANDLE h = INVALID_HANDLE_VALUE;
            {
                std::lock_guard lock(clientsMu_);
                if (auto it = clients_.find(id); it != clients_.end())
                    h = it->second.handle;
            }
            if (h == INVALID_HANDLE_VALUE) continue;
            ClientBuf tmp{h, {}};
            WriteClient(tmp, payload);
        }
    }

public:

    void ReaderLoop() {
        while (running_) AcceptOne();
    }

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::thread readerThread_;
    std::atomic<bool> running_{true};
    std::mutex clientsMu_;
    std::map<uint64_t, ClientBuf> clients_;
    std::mutex outboxMu_;
    std::vector<std::pair<uint64_t, std::string>> outbox_;
    uint64_t nextClientId_ = 1;
};

} // namespace

ControlServer& ControlServer::Get() {
    static PipeServer instance;
    return instance;
}

} // namespace ow
