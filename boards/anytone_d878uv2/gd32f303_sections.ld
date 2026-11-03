/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Tommi Tauriainen */

/* Sections of every GD32F303 image: the app, the loader and its stub. The
 * file that includes this one sets MEMORY (FLASH, RAM, NOINIT, HANDOVER)
 * and __main_stack_size.
 *
 * The top 4 KB of RAM is left to the vendor boot loader. Just below it, a
 * small area survives a restart: the crash record, then the 16-byte
 * hand-over block at 0x20016FF0. */

ENTRY(resetHandler)

SECTIONS
{
    .vectors : ALIGN(512)
    {
        KEEP(*(.vectors))
    } > FLASH

    /* The image info block, right after the 84-entry vector table. */
    .image_info :
    {
        KEEP(*(.image_info))
    } > FLASH
    ASSERT(SIZEOF(.image_info) == 0 || ADDR(.image_info) == ORIGIN(FLASH) + 84 * 4, "the image info block must follow the vector table")

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

    /* The hand-over block to the loader, the same in every image. */
    .handover (NOLOAD) :
    {
        KEEP(*(.handover))
    } > HANDOVER

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
