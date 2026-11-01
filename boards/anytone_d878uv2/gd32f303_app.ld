/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Tommi Tauriainen */

/* GD32F303 with 1 MB flash and 96 KB RAM, behind the vendor boot loader.
 * The image starts at 0x08004000 and must end below 0x080F5800: the flash
 * above holds radio data.
 *
 * The top 4 KB of RAM is left to the boot loader. Just below it, a small
 * area survives a restart for the crash record. */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x08004000, LENGTH = 0x080F5800 - 0x08004000
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 0x16F00
    NOINIT (rw): ORIGIN = 0x20016F00, LENGTH = 256
}

ENTRY(resetHandler)

/* Main stack: start-up code and interrupt handlers. Tasks have their own. */
__main_stack_size = 2K;

SECTIONS
{
    .vectors : ALIGN(512)
    {
        KEEP(*(.vectors))
    } > FLASH

    .text :
    {
        /* Time-critical code first, so it stays in the flash executed
           without wait states. */
        *(.text.hot .text.hot.*)
        KEEP(*(.probe_near.*))
        *(.text .text.*)
        __code_end = .;
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH

    .init_array :
    {
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } > FLASH

    .data :
    {
        . = ALIGN(4);
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM AT > FLASH
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM


    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        __stack_bottom = .;
        . += __main_stack_size;
        __stack_top = .;
    } > RAM

    /* Kept over a restart: not cleared at start-up. */
    .noinit (NOLOAD) :
    {
        *(.noinit .noinit.*)
    } > NOINIT

    __image_end = LOADADDR(.data) + SIZEOF(.data);

    /* The flash speed probe's copies at fixed addresses (DMM_FLASH_PROBE
       builds; otherwise empty and dropped). */
    .probe_far0 0x08040000 :
    {
        KEEP(*(.probe_far0.*))
    } > FLASH
    .probe_far1 0x08080000 :
    {
        KEEP(*(.probe_far1.*))
    } > FLASH
}
