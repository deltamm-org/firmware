# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Henri Lavikainen

# Warns when an image's code (the __code_end symbol from the linker script)
# reaches past LIMIT, the end of the flash the MCU executes without wait
# states. Usage: cmake -DNM=<nm> -DELF=<file> -DLIMIT=<hex> -P CheckCodeEnd.cmake

execute_process(COMMAND ${NM} ${ELF} OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "nm failed on ${ELF}")
endif()
if(NOT out MATCHES "([0-9a-fA-F]+) [A-Za-z] __code_end")
    message(FATAL_ERROR "${ELF} has no __code_end symbol")
endif()
math(EXPR code_end "0x${CMAKE_MATCH_1}")
math(EXPR limit "${LIMIT}")
math(EXPR code_end_hex "${code_end}" OUTPUT_FORMAT HEXADECIMAL)
if(code_end GREATER limit)
    math(EXPR over "${code_end} - ${limit}")
    message(WARNING "Code ends at ${code_end_hex}, ${over} bytes past ${LIMIT}: that code runs with flash wait states. Move time-critical code first in .text.")
else()
    math(EXPR left "${limit} - ${code_end}")
    message(STATUS "Code ends at ${code_end_hex}, ${left} bytes below ${LIMIT}")
endif()
