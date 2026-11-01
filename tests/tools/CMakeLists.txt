# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

add_executable(dmm_radio_tests ../main.cpp test_dmm_radio.cpp test_update_files.cpp test_update_client.cpp test_deltamm_client.cpp
    test_loader_client.cpp fake_radio.h fake_boot_loader.h fake_loader.h)
target_link_libraries(dmm_radio_tests PRIVATE dmm_radio_lib dmm_services doctest::doctest)
target_include_directories(dmm_radio_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../services)
dmm_warnings(dmm_radio_tests)
add_test(NAME dmm_radio_tests COMMAND dmm_radio_tests)

