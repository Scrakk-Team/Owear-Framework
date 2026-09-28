# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
if(OW_PLATFORM STREQUAL "mac")
    set(OW_THEME_LIBS "-framework Cocoa")
endif()
ow_add_module(theme   SOURCES src/theme_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${OW_GUI_LIBS} ${OW_THEME_LIBS}
    INCLUDES ${OW_GUI_INCS})
