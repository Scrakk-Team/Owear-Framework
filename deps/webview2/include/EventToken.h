// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// EventToken.h mínimo — WebView2.h lo incluye y el SDK de Windows a veces no
// lo trae en rutas de include de MinGW. Definición canónica de la WRL.
#pragma once
// Mismo guard que el SDK de Windows (winrt/eventtoken.h) para no redefinir
// EventRegistrationToken cuando ambos headers acaban en la misma unidad.
#ifndef __eventtoken_h__
#define __eventtoken_h__

typedef struct EventRegistrationToken {
    long long value;
} EventRegistrationToken;

#endif // __eventtoken_h__
