/*
 * MyOS bootloader linker script (Milestone 14).
 *
 * Targets the Boot Loader Section this bootloader will EVENTUALLY be
 * installed into once ISP hardware is available (see docs/bootloader.md's
 * "Deferred" section) -- building this file does not install or flash it
 * anywhere by itself; `make bootloader` only produces
 * build/bootloader.elf/.hex sitting in build/, same as any other object.
 *
 * BOOTSZ1:0 = 10 (1KB boot section @ 0x7C00-0x7FFF) is the size this
 * project has chosen (see docs/bootloader.md) -- not the factory default
 * 512B @ 0x7E00 that stock Optiboot uses. Note this fuse change itself is
 * also part of the deferred ISP-only install step, not performed by
 * anything in this build.
 */

ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00007C00, LENGTH = 0x400
    SRAM  (rwx) : ORIGIN = 0x00800100, LENGTH = 0x800
}

SECTIONS
{
    .text :
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.progmem.data)
        *(.progmem.data.*)
        _etext = .;
    } > FLASH

    /* Included defensively, even though boot_main.c/boot_start.S are
     * written to need neither (see boot_start.S's top comment) -- if the
     * compiler ever emits something trivial into either anyway (e.g. a
     * hidden constant pool), this gives it somewhere to go instead of a
     * link error, and avr-size/objdump on the actual build output is what
     * confirms whether that ever happens (see docs/bootloader.md). */
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
}
