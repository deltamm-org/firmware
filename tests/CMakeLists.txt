# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(doctest)

add_executable(tests
    main.cpp
    lib/test_containers.cpp
    lib/test_format.cpp
    lib/test_strings.cpp
    lib/test_tone_synth.cpp
    drivers/test_rda1846.cpp
    drivers/test_sct3258.cpp
    drivers/test_spi_nand.cpp
    drivers/test_st7735.cpp
    drivers/test_usb.cpp)
target_link_libraries(tests PRIVATE dmm_lib dmm_drivers_usb dmm_drivers_display dmm_drivers_storage dmm_drivers_baseband dmm_drivers_radio doctest::doctest)
target_compile_definitions(tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
dmm_core_options(tests)
if(TARGET dmm_os)
    target_sources(tests PRIVATE os/test_os.cpp services/test_log.cpp services/test_power.cpp services/test_text.cpp services/test_input.cpp services/test_battery.cpp services/test_battery_saver.cpp services/test_d878uv2_storage.cpp services/test_data_link.cpp services/test_baseband.cpp services/test_audio.cpp services/test_d878uv2_calibration.cpp services/test_fm.cpp services/test_key_lock.cpp services/test_d878uv2_codeplug.cpp services/test_d878uv2_codeplug_writer.cpp services/test_d878uv2_programming.cpp services/test_dmr.cpp services/test_roaming.cpp services/test_channel_selector.cpp services/test_channel_selector.cpp services/test_nmea.cpp services/test_casic_receiver.cpp services/test_clock.cpp services/test_gps_schedule.cpp)
    target_link_libraries(tests PRIVATE dmm_os dmm_services)
endif()
add_test(NAME tests COMMAND tests)

# The core libraries must not pull in the heap, exceptions or printf.
set(DMM_CORE_LIBS $<TARGET_FILE:dmm_lib> $<TARGET_FILE:dmm_drivers_usb> $<TARGET_FILE:dmm_drivers_display> $<TARGET_FILE:dmm_drivers_storage> $<TARGET_FILE:dmm_drivers_baseband> $<TARGET_FILE:dmm_drivers_radio>)
if(TARGET dmm_services)
    list(APPEND DMM_CORE_LIBS $<TARGET_FILE:dmm_services>)
endif()
add_test(NAME core-heap-free
    COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} "-DLIBS=${DMM_CORE_LIBS}"
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckHeapFree.cmake)
