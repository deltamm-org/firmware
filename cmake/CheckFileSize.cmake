# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Henri Lavikainen

# Fails when FILE is larger than LIMIT bytes, and prints its size.
# Usage: cmake -DFILE=<file> -DLIMIT=<bytes> -P CheckFileSize.cmake

file(SIZE ${FILE} size)
get_filename_component(name ${FILE} NAME)
if(size GREATER LIMIT)
    message(FATAL_ERROR "${name} is ${size} bytes, more than its ${LIMIT}")
endif()
message(STATUS "${name}: ${size} bytes of ${LIMIT}")
