# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

# Flags every DeltaMM target gets. Core targets (lib, os, services, hal,
# drivers) also get dmm_core_options(): no exceptions, no RTTI, and no
# heap, checked by the core-heap-free test.

function(dmm_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /wd5030)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
    endif()
endfunction()

function(dmm_core_options target)
    dmm_warnings(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /EHs-c- /GR-)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>)
    endif()
endfunction()
