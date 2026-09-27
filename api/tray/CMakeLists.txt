# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# ── tray ── Linux: GtkStatusIcon (GTK3, SIN deps extra). Windows: Shell_NotifyIcon.
if(OW_PLATFORM STREQUAL "mac")
    set(TRAY_LIBS_STR "-framework Cocoa")
elseif(OW_PLATFORM STREQUAL "win")
    set(TRAY_LIBS_STR "shell32;user32;gdi32;gdiplus")
endif()
ow_add_module(tray
    SOURCES src/tray_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${TRAY_LIBS_STR} ${OW_GUI_LIBS}
    INCLUDES ${OW_GUI_INCS})
