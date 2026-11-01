/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Tommi Tauriainen */

/* GD32F303 with 1 MB flash and 96 KB RAM, behind the vendor boot loader.
 * The image starts at 0x08004000 and must end below 0x080F5800: the flash
 * above holds radio data. */

MEMORY
{
    FLASH (rx)    : ORIGIN = 0x08004000, LENGTH = 0x080F5800 - 0x08004000
    RAM (rwx)     : ORIGIN = 0x20000000, LENGTH = 0x16F00
    NOINIT (rw)   : ORIGIN = 0x20016F00, LENGTH = 0xF0
    HANDOVER (rw) : ORIGIN = 0x20016FF0, LENGTH = 16
}

/* Main stack: start-up code and interrupt handlers. Tasks have their own. */
__main_stack_size = 2K;

INCLUDE gd32f303_sections.ld
