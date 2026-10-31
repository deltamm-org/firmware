# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

# Pieces both simulators use.
add_library(dmm_sim_common STATIC sim_display.h sim_display.cpp include/dmm/board/sim_input.h sim_input.cpp sim_audio.h include/dmm/board/sim_gps.h sim_gps.cpp include/dmm/board/sim_rtc.h include/dmm/board/sim_modem.h include/dmm/board/sim_aprs_link.h include/dmm/board/sim_battery.h include/dmm/board/sim_fm.h include/dmm/board/parts.h sim_storage.h sim_storage.cpp)
target_include_directories(dmm_sim_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dmm_sim_common PUBLIC dmm_services Qt6::Gui)
dmm_warnings(dmm_sim_common)
