# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# ── API path ──────────────────────────────────────────────────────────────────
ow_add_module(path
    SOURCES
        src/path.cpp
        src/dirs_${OW_PLATFORM}.${OW_SRC_EXT})
