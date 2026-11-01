/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Henri Lavikainen */

/* DeltaMM's loader: 32 KB just below the radio data's sector, at
 * 0x080ED000-0x080F5000 (services/include/dmm/services/d878uv2_boot_layout.h).
 * dmm-radio appends a CRC-32 of the image after its end, which the stub
 * checks; the last 4 bytes stay free for it. */

MEMORY
{
    FLASH (rx)    : ORIGIN = 0x080ED000, LENGTH = 0x8000 - 4
    RAM (rwx)     : ORIGIN = 0x20000000, LENGTH = 0x16F00
    NOINIT (rw)   : ORIGIN = 0x20016F00, LENGTH = 0xF0
    HANDOVER (rw) : ORIGIN = 0x20016FF0, LENGTH = 16
}

__main_stack_size = 4K;

INCLUDE gd32f303_sections.ld
