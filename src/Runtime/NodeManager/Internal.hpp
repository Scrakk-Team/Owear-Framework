// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager/Internal.hpp — helpers internos de NodeManager.
#pragma once
#include "../NodeManager.hpp"

#include <string>

namespace ow {
namespace nodemanager_detail {

/// Compara dos versiones semver ("v22.4.0"). <0, 0, >0.
int SemverCompare(const std::string& a, const std::string& b);

/// ¿`version` ("v22.12.0") satisface `range` ("latest"|"lts"|"v22"|"22.12.0")?
bool SatisfiesRange(const std::string& version, const std::string& range);

} // namespace nodemanager_detail
} // namespace ow
