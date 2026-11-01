# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Henri Lavikainen

# The loader and its stub: images of their own, built next to the app.
# Their binaries go to the app's folder: dmm_stub.bin and dmm_loader.bin.

# Unoptimised code does not fit their 4 KB and 32 KB.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "The loader and its stub are not built in Debug builds; use a release build")
    return()
endif()

set(_board ${CMAKE_CURRENT_SOURCE_DIR}/..)
get_filename_component(_out "${CMAKE_CURRENT_BINARY_DIR}/.." ABSOLUTE)

function(dmm_boot_image target script limit)
    target_link_libraries(${target} PRIVATE dmm_boot dmm_hal_gd32f30x dmm_drivers_radio dmm_version)
    target_include_directories(${target} PRIVATE ${_board})
    dmm_core_options(${target})
    if(DMM_KEEP_SWD)
        target_compile_definitions(${target} PRIVATE DMM_KEEP_SWD)
    endif()
    target_link_options(${target} PRIVATE
        -nostartfiles
        -L${_board}
        -T${CMAKE_CURRENT_SOURCE_DIR}/${script}
        -Wl,-Map=${_out}/${target}.map
        -Wl,--print-memory-usage
        -Wl,--no-warn-rwx-segments)
    set_target_properties(${target} PROPERTIES SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY ${_out}
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${script};${_board}/gd32f303_sections.ld")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} "-DLIBS=$<JOIN:$<TARGET_OBJECTS:${target}>,|>" -P ${PROJECT_SOURCE_DIR}/cmake/CheckHeapFree.cmake
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> ${_out}/${target}.bin
        COMMAND ${CMAKE_COMMAND} -DFILE=${_out}/${target}.bin -DLIMIT=${limit} -P ${PROJECT_SOURCE_DIR}/cmake/CheckFileSize.cmake
        COMMENT "Writing ${target}.bin"
        VERBATIM)
endfunction()

add_executable(dmm_stub stub.cpp)
dmm_boot_image(dmm_stub gd32f303_stub.ld 4096)

add_executable(dmm_loader loader_main.cpp)
target_link_libraries(dmm_loader PRIVATE dmm_drivers_usb)
dmm_boot_image(dmm_loader gd32f303_loader.ld 32764)
