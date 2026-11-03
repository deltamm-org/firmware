# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

add_library(dmm_radio_lib STATIC
    transport.h
    cps_client.h cps_client.cpp
    backup.h backup.cpp
    update_client.h update_client.cpp
    update_files.h update_files.cpp
    deltamm_client.h deltamm_client.cpp zlib_crc.h
    loader_client.h loader_client.cpp
    loader_images.h loader_images.cpp
    stock_layout.h stock_layout.cpp)
target_include_directories(dmm_radio_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dmm_radio_lib PUBLIC Qt6::Core dmm_version dmm_lib dmm_boot)
dmm_warnings(dmm_radio_lib)

if(TARGET Qt6::SerialPort)
    add_executable(dmm-radio main.cpp serial_transport.h serial_transport.cpp)
    target_link_libraries(dmm-radio PRIVATE dmm_radio_lib Qt6::SerialPort)
    dmm_warnings(dmm-radio)
endif()
