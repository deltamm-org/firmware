# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

add_library(dmm_services STATIC
    src/battery.cpp
    src/channel_selector.cpp
    src/d878uv2_calibration.cpp
    src/d878uv2_codeplug.cpp
    src/d878uv2_storage.cpp
    src/data_link.cpp
    src/dmr.cpp
    src/input.cpp
    src/key_lock.cpp
    src/log.cpp
    src/power.cpp
    src/audio.cpp
    src/rda1846_transceiver.cpp
    src/sct3258_baseband.cpp
    src/text.cpp
    src/usb_log.cpp
    src/fonts/spleen_5x8.cpp
    src/fonts/spleen_6x12.cpp
    src/fonts/spleen_8x16.cpp
    src/fonts/spleen_12x24.cpp)
target_include_directories(dmm_services PUBLIC include)
target_link_libraries(dmm_services PUBLIC dmm_lib dmm_os dmm_drivers_usb dmm_drivers_storage dmm_drivers_baseband dmm_drivers_radio)
dmm_core_options(dmm_services)
