// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/Platform/win/SameUser.cpp — verificacion mismo-usuario del named pipe.
#include "Internal.hpp"

#include "../../../Core/Log.hpp"

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace ow {
namespace control_win_detail {


// VERIFICAR-EN-WINDOWS: verificación mínima de "mismo usuario" para el pipe
// de control. Impersona al cliente conectado, compara su SID de usuario con
// el de este proceso, y revierte la impersonación. No se ha podido compilar
// ni probar en Windows real; revisar con cuidado antes de confiar en esta
// mitigación (en particular el manejo de errores de las APIs de tokens).
bool ClientIsSameUser(HANDLE pipeHandle) {
    if (!ImpersonateNamedPipeClient(pipeHandle)) {
        log::Warn("control", "ImpersonateNamedPipeClient falló");
        return false;
    }

    bool sameUser = false;
    HANDLE clientToken = nullptr;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &clientToken)) {
        DWORD clientNeeded = 0;
        GetTokenInformation(clientToken, TokenUser, nullptr, 0, &clientNeeded);
        std::vector<uint8_t> clientBuf(clientNeeded);
        if (clientNeeded > 0 &&
            GetTokenInformation(clientToken, TokenUser, clientBuf.data(),
                                clientNeeded, &clientNeeded)) {
            auto* clientUser = reinterpret_cast<TOKEN_USER*>(clientBuf.data());

            HANDLE selfToken = nullptr;
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &selfToken)) {
                DWORD selfNeeded = 0;
                GetTokenInformation(selfToken, TokenUser, nullptr, 0, &selfNeeded);
                std::vector<uint8_t> selfBuf(selfNeeded);
                if (selfNeeded > 0 &&
                    GetTokenInformation(selfToken, TokenUser, selfBuf.data(),
                                        selfNeeded, &selfNeeded)) {
                    auto* selfUser = reinterpret_cast<TOKEN_USER*>(selfBuf.data());
                    sameUser = EqualSid(clientUser->User.Sid, selfUser->User.Sid);
                }
                CloseHandle(selfToken);
            }
        }
        CloseHandle(clientToken);
    } else {
        log::Warn("control", "OpenThreadToken falló al verificar el cliente del pipe");
    }

    RevertToSelf();
    return sameUser;
}

} // namespace control_win_detail
} // namespace ow
