/*
 * MyOS linker script — ATmega328P, Phase 1 (Optiboot present).
 *
 * Optiboot on the Uno lives at the top of flash (0x7C00-0x7FFF, 1KB).
 * FLASH length is capped below that so the app image never overlaps it.
 * Once Milestone 14 (custom bootloader) lands, this boundary is revisited.
 */

ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 0x7C00
    SRAM  (rwx) : ORIGIN = 0x00800100, LENGTH = 0x800
}

SECTIONS
{
    .text :
    {
        *(.vectors)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        _etext = .;
    } > FLASH

    .data :
    {
        _data_start = .;
        *(.data)
        *(.data.*)
        _data_end = .;
    } > SRAM AT> FLASH

    _data_load_start = LOADADDR(.data);

    .bss :
    {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    } > SRAM

    _heap_start = .;
    _stack_top  = ORIGIN(SRAM) + LENGTH(SRAM) - 1;
}
