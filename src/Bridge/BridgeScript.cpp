// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Bridge/BridgeScript.cpp — compone el bridge JS inyectado en el renderer.
#include "Script/Internal.hpp"

#include <string>

namespace ow {

std::string BuildBridgeScript() { return ScriptCore() + ScriptApi(); }

} // namespace ow
