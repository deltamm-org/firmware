# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

include(BasebandBootBlob)
dmm_baseband_boot_blob(dmm_baseband_boot_blob "${DMM_STOCK_FIRMWARE}")

add_executable(deltamm main.cpp pins.h panel_bus.h board_display.h board_display.cpp include/dmm/board/board_input.h board_input.cpp board_adc.h include/dmm/board/board_battery.h board_battery.cpp include/dmm/board/parts.h board_baseband.h board_baseband.cpp board_audio.h board_audio.cpp board_rf.h board_rf.cpp board_fm_tuning.h board_fm_tuning.cpp include/dmm/board/board_gps.h board_gps.cpp include/dmm/board/board_rtc.h board_rtc.cpp)
target_link_libraries(deltamm PRIVATE dmm_app dmm_drivers_display dmm_drivers_storage dmm_drivers_baseband dmm_baseband_boot_blob
    dmm_hal_gd32f30x)
dmm_core_options(deltamm)

# Keeps the debug port (SWD) on, at the cost of PF2, whose pin it shares.
option(DMM_KEEP_SWD "Keep the SWD debug port; PF2 then does not work" OFF)
if(DMM_KEEP_SWD)
    target_compile_definitions(deltamm PRIVATE DMM_KEEP_SWD)
endif()
# A test image that times code and data reads at three places in flash and
# logs the result at start. The image grows past 512 KB.
option(DMM_FLASH_PROBE "Build the flash speed probe into the image" OFF)
if(DMM_FLASH_PROBE)
    target_sources(deltamm PRIVATE flash_probe.h flash_probe.cpp)
    target_compile_definitions(deltamm PRIVATE DMM_FLASH_PROBE)
endif()

# End of the flash the GD32F303 executes without wait states (the first
# 256 KB, GD32F30x User Manual, flash memory controller chapter).
set(DMM_ZERO_WAIT_FLASH_END 0x08040000)

target_link_options(deltamm PRIVATE
    -nostartfiles
    -T${CMAKE_CURRENT_SOURCE_DIR}/gd32f303_app.ld
    -Wl,-Map=$<TARGET_FILE_DIR:deltamm>/deltamm.map
    -Wl,--print-memory-usage
    -Wl,--no-warn-rwx-segments)
set_target_properties(deltamm PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gd32f303_app.ld)

# The core libraries must not pull in the heap, exceptions or printf.
set(_core_libs dmm_lib dmm_boot dmm_os dmm_drivers_usb dmm_drivers_display dmm_drivers_storage dmm_drivers_baseband dmm_drivers_radio dmm_services dmm_app dmm_hal_gd32f30x)
set(_core_files)
foreach(lib IN LISTS _core_libs)
    list(APPEND _core_files $<TARGET_FILE:${lib}>)
endforeach()

add_custom_command(TARGET deltamm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} "-DLIBS=$<JOIN:${_core_files},|>" -P ${PROJECT_SOURCE_DIR}/cmake/CheckHeapFree.cmake
    COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DELF=$<TARGET_FILE:deltamm> -DLIMIT=${DMM_ZERO_WAIT_FLASH_END} -P ${PROJECT_SOURCE_DIR}/cmake/CheckCodeEnd.cmake
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:deltamm> $<TARGET_FILE_DIR:deltamm>/deltamm.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:deltamm>
    COMMENT "Checking the core libraries and writing deltamm.bin"
    VERBATIM)

# Firmware files for the vendor update tool, made by dmm-radio from this
# image and the user's own factory firmware files.
set(DMM_RADIO_TOOL "" CACHE FILEPATH "dmm-radio from a host build, for the update-files target")
if(DMM_RADIO_TOOL AND DMM_STOCK_FIRMWARE)
    add_custom_target(update-files
        COMMAND ${DMM_RADIO_TOOL} prepare $<TARGET_FILE_DIR:deltamm>/deltamm.bin ${DMM_STOCK_FIRMWARE}
                $<TARGET_FILE_DIR:deltamm>/package --name DeltaMM_${PROJECT_VERSION}
        DEPENDS deltamm
        USES_TERMINAL)
endif()
