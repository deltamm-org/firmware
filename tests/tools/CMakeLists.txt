# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

add_executable(dmm_radio_tests ../main.cpp test_dmm_radio.cpp test_update_files.cpp test_update_client.cpp test_deltamm_client.cpp
    test_loader_client.cpp fake_radio.h fake_boot_loader.h fake_loader.h)
target_link_libraries(dmm_radio_tests PRIVATE dmm_radio_lib dmm_services doctest::doctest)
target_include_directories(dmm_radio_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../services)
dmm_warnings(dmm_radio_tests)
add_test(NAME dmm_radio_tests COMMAND dmm_radio_tests)

# dmm-radio update, confirm, rollback and loader against the loader
# simulator, with power cuts (Linux and macOS).
if(TARGET loader_sim AND TARGET dmm-radio)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME loader-sim-update
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/loader_sim_e2e.py $<TARGET_FILE:loader_sim>
                    $<TARGET_FILE:dmm-radio> ${CMAKE_CURRENT_BINARY_DIR}/loader_sim_e2e)
        set_tests_properties(loader-sim-update PROPERTIES TIMEOUT 300)
    endif()
endif()
