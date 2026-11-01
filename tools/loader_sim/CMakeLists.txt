# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Henri Lavikainen

# The loader on the PC: a file as flash, a pseudo-terminal as USB.
add_executable(loader_sim main.cpp)
target_link_libraries(loader_sim PRIVATE dmm_boot dmm_services dmm_version)
dmm_warnings(loader_sim)
