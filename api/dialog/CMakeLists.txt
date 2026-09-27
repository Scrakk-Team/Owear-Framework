# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
if(OW_PLATFORM STREQUAL "win")
    set(OW_DLG_LIBS comctl32)
endif()
ow_add_module(dialog    SOURCES src/dialog_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${OW_GUI_LIBS} ${OW_DLG_LIBS}
    INCLUDES ${OW_GUI_INCS})
