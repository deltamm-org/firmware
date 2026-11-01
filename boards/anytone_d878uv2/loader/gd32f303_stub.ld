/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Henri Lavikainen */

/* The loader's stub: one 4 KB sector at 0x08004000, where the vendor boot
 * loader starts an image. It starts the loader, or slot A without it. */

MEMORY
{
    FLASH (rx)    : ORIGIN = 0x08004000, LENGTH = 0x1000
    RAM (rwx)     : ORIGIN = 0x20000000, LENGTH = 0x16F00
    NOINIT (rw)   : ORIGIN = 0x20016F00, LENGTH = 0xF0
    HANDOVER (rw) : ORIGIN = 0x20016FF0, LENGTH = 16
}

__main_stack_size = 1K;

INCLUDE gd32f303_sections.ld
