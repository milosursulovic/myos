/*
 * MyOS linker script — ATmega328P, Phase 1 (Optiboot present).
 *
 * The stock Arduino Uno Optiboot bootloader only actually needs 512 bytes
 * (0x7E00-0x7FFF; Arduino's own boards.txt sets upload.maximum_size=32256
 * = 32768-512) — NOT 1KB, correcting what this comment used to claim.
 * FLASH length here is capped at 0x7C00 anyway (1KB reserved, more than
 * Optiboot itself needs) because that's the boot-section boundary Milestone
 * 14's own custom bootloader targets (BOOTSZ1:0=10, 1KB @ 0x7C00) — see
 * docs/bootloader.md. Deliberately larger than Optiboot's actual 512-byte
 * footprint so the app image never overlaps either bootloader.
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
        *(.progmem.data)
        *(.progmem.data.*)
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
