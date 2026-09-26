# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
ow_add_module(screen    SOURCES src/screen_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${OW_GUI_LIBS}
    INCLUDES ${OW_GUI_INCS})
