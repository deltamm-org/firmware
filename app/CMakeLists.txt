# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

add_library(dmm_app STATIC src/app.cpp src/audio.cpp src/audio.h src/input.cpp src/input.h src/radio.cpp src/radio.h src/system.cpp src/system.h src/ui.cpp src/ui.h src/standby.cpp src/standby.h src/gps.cpp src/gps.h src/aprs.cpp src/aprs.h)
target_include_directories(dmm_app PUBLIC include ${DMM_PARTS_DIR})
target_link_libraries(dmm_app PUBLIC dmm_services dmm_version)
dmm_core_options(dmm_app)
