# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Henri Lavikainen

add_executable(afsk-decode main.cpp audio_file.h audio_file.cpp)
target_link_libraries(afsk-decode PRIVATE dmm_services)
dmm_warnings(afsk-decode)
