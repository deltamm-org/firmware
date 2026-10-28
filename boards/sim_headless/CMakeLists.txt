# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

add_executable(sim main.cpp)
target_link_libraries(sim PRIVATE dmm_app dmm_sim_common)
dmm_warnings(sim)

add_test(NAME sim-boots COMMAND sim --seconds 2 --leds)
set_tests_properties(sim-boots PROPERTIES
    PASS_REGULAR_EXPRESSION "DeltaMM [0-9.]+ on sim_headless.*LED red on.*LED red off")
