// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// macOS usa la MISMA base fork/exec que Linux.
// VERIFICAR-EN-MACOS: execvpe no existe → env con setenv como en Linux.
#include "spawn_linux.cpp"
