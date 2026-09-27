// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager_win.cpp — spawn del sidecar (CreateProcess).
//
#include "NodeManager.hpp"
#include "../Control/ControlServer.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace ow {

namespace {
/// PID del sidecar Node (para terminarlo al salir del kernel).
long g_sidecarPid = -1;
/// Job que agrupa el sidecar: cerrarlo lo mata (aunque el kernel muera de golpe).
HANDLE g_sidecarJob = nullptr;
} // namespace

long NodeManager::Spawn(const std::filesystem::path& nodeBin,
                        const std::string& entryJs) {
    std::string sockPath = ControlServer::Get().SocketPath();
    if (!sockPath.empty()) SetEnvironmentVariableA("OW_CONTROL_SOCKET", sockPath.c_str());

    std::string cmdline = "\"" + nodeBin.string() + "\" \"" + entryJs + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                             0, nullptr, nullptr, &si, &pi);
    if (!ok) return -1;
    CloseHandle(pi.hThread);

    // Best-effort: asignar el sidecar a un Job con KILL_ON_JOB_CLOSE. Si el
    // kernel muere (incluso por kill duro), el Job se cierra y el sidecar cae.
    if (!g_sidecarJob) g_sidecarJob = CreateJobObjectA(nullptr, nullptr);
    if (g_sidecarJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_sidecarJob, JobObjectExtendedLimitInformation, &info,
                                sizeof(info));
        AssignProcessToJobObject(g_sidecarJob, pi.hProcess);
    }

    CloseHandle(pi.hProcess);
    g_sidecarPid = static_cast<long>(pi.dwProcessId);
    return static_cast<long>(pi.dwProcessId);
}

void NodeManager::ShutdownSidecar() {
    if (g_sidecarJob) {
        CloseHandle(g_sidecarJob); // KILL_ON_JOB_CLOSE termina el sidecar
        g_sidecarJob = nullptr;
    }
    if (g_sidecarPid > 0) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                               static_cast<DWORD>(g_sidecarPid));
        if (h) {
            TerminateProcess(h, 0);
            WaitForSingleObject(h, 1000);
            CloseHandle(h);
        }
    }
    g_sidecarPid = -1;
}

} // namespace ow
