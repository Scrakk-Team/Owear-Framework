// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager_linux.cpp — spawn del sidecar (fork/exec).
//
#include "NodeManager.hpp"
#include "../Control/ControlServer.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace ow {

namespace {
/// PID del sidecar Node (para terminarlo al salir del kernel).
long g_sidecarPid = -1;
} // namespace

long NodeManager::Spawn(const std::filesystem::path& nodeBin,
                        const std::string& entryJs) {
    std::string bin = nodeBin.string();
    std::string binDir = nodeBin.parent_path().string();

    // PATH con el runtime gestionado al frente
    const char* oldPathC = std::getenv("PATH");
    std::string newPath = binDir + ":";
    if (oldPathC) newPath += oldPathC;

    std::string sockEnv;
    {
        auto& cs = ControlServer::Get();
        if (!cs.SocketPath().empty()) sockEnv = "OW_CONTROL_SOCKET=" + cs.SocketPath();
    }

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        // hijo
        // Si el kernel muere (incluso con SIGKILL), el sidecar se va con él.
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        setenv("OW_CONTROL_SOCKET", ControlServer::Get().SocketPath().c_str(), 1);
        setenv("PATH", newPath.c_str(), 1);
        setenv("NODE_ENV", std::getenv("NODE_ENV") ? std::getenv("NODE_ENV") : "development",
               0);
        execl(bin.c_str(), bin.c_str(), entryJs.c_str(),
              static_cast<char*>(nullptr));
        _exit(127); // exec falló
    }
    g_sidecarPid = static_cast<long>(pid);
    return static_cast<long>(pid);
}

void NodeManager::ShutdownSidecar() {
    if (g_sidecarPid <= 0) return;
    const long pid = g_sidecarPid;
    g_sidecarPid = -1;
    if (::kill(pid, SIGTERM) != 0) return;
    // Margen breve para una salida limpia; luego SIGKILL.
    for (int i = 0; i < 20; ++i) {
        if (::kill(pid, 0) != 0) return; // ya no existe
        usleep(10 * 1000);
    }
    ::kill(pid, SIGKILL);
}

} // namespace ow
