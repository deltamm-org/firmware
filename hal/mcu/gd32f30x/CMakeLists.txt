# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

add_library(dmm_hal_gd32f30x STATIC
    src/adc.cpp
    src/bitbang_spi_nand_bus.cpp
    src/clock.cpp
    src/exti.cpp
    src/gpio.cpp
    src/parallel_host_port.cpp
    src/three_wire_bus.cpp
    src/parallel_lcd_bus.cpp
    src/dac.cpp
    src/pwm.cpp
    src/pwm_tone.cpp
    src/reset.cpp
    src/startup.cpp
    src/step_timer.cpp
    src/usbd.cpp
    src/watchdog.cpp)
target_include_directories(dmm_hal_gd32f30x PUBLIC include)
target_link_libraries(dmm_hal_gd32f30x PUBLIC dmm_hal dmm_hal_cortex_m dmm_lib)
dmm_core_options(dmm_hal_gd32f30x)
