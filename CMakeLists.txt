# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

cmake_minimum_required(VERSION 3.25)
project(deltamm
    VERSION 0.20.1
    DESCRIPTION "Open firmware for DMR and FM handheld radios"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DmmOptions)
include(FetchContent)

if(CMAKE_CROSSCOMPILING)
    set(DMM_BOARD "anytone_d878uv2" CACHE STRING "Firmware board folder under boards/")
    set(DMM_STOCK_FIRMWARE "" CACHE FILEPATH "The user's factory firmware .CDD (SCT3258 boot program, update files)")
    include(boards/${DMM_BOARD}/board.cmake)
else()
    # Qt is optional: without it only the core libraries and their tests build.
    find_package(Qt6 6.8 QUIET COMPONENTS Core Gui Quick QuickControls2 SerialPort)
    if(Qt6_FOUND)
        qt_standard_project_setup(REQUIRES 6.8)
    endif()
endif()

configure_file(cmake/templates/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/dmm/version.h @ONLY)
add_library(dmm_version INTERFACE)
target_include_directories(dmm_version INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_subdirectory(lib)
add_subdirectory(hal)
add_subdirectory(drivers)
add_subdirectory(os)
# The board's parts header (dmm/board/parts.h): the one board header the
# app includes. Host builds use the simulators' parts.
if(CMAKE_CROSSCOMPILING)
    set(DMM_PARTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/boards/${DMM_BOARD}/include)
else()
    set(DMM_PARTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/boards/sim_common/include)
endif()

if(TARGET dmm_os)
    add_subdirectory(services)
    add_subdirectory(app)
endif()
if(CMAKE_CROSSCOMPILING)
    add_subdirectory(boards/${DMM_BOARD})
endif()

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_subdirectory(tests)
    if(TARGET Qt6::Gui)
        add_subdirectory(boards/sim_common)
        add_subdirectory(boards/sim_headless)
    endif()
    if(TARGET Qt6::Quick)
        add_subdirectory(boards/sim_qt)
    endif()
    if(TARGET Qt6::Core)
        add_subdirectory(tools/dmm_radio)
        add_subdirectory(tests/tools)
    endif()
endif()
