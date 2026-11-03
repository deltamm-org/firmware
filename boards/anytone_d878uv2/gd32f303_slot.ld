/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Henri Lavikainen */

/* The app for a radio with DeltaMM's loader: linked for slot A at
 * 0x08005000 and no larger than slot B (0x08078000-0x080EA000), so the
 * loader can keep the previous image there. The numbers are those of
 * services/include/dmm/services/d878uv2_boot_layout.h. */

MEMORY
{
    FLASH (rx)    : ORIGIN = 0x08005000, LENGTH = 0x080EA000 - 0x08078000
    RAM (rwx)     : ORIGIN = 0x20000000, LENGTH = 0x16F00
    NOINIT (rw)   : ORIGIN = 0x20016F00, LENGTH = 0xF0
    HANDOVER (rw) : ORIGIN = 0x20016FF0, LENGTH = 16
}

/* Main stack: start-up code and interrupt handlers. Tasks have their own. */
__main_stack_size = 2K;

INCLUDE gd32f303_sections.ld
