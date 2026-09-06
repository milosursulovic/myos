#include <avr/io.h>

/* Bootloader reset entry (Milestone 14). Deliberately much smaller than
 * boot/start.S's _start, for two independent reasons:
 *
 * 1. No vector table. boot/start.S's _start is reached via `jmp _start` at
 *    flash address 0x0000, because the kernel always resets with the
 *    hardware's default reset vector (address 0x0000) and needs a real
 *    jump table there for Timer0/USART interrupts. This bootloader is a
 *    different story: once installed (deferred -- see docs/bootloader.md),
 *    it's entered via the BOOTRST fuse, which changes the *hardware reset
 *    vector itself* to the start of the Boot Loader Section -- execution
 *    begins directly at this file's _start, with no jump table involved at
 *    all. `bootloader/linker.ld` places this file's .text first, so
 *    _start also lands at the very start of that section. There is no
 *    vector table anywhere in this image: boot_main.c's top-of-file
 *    comment explains why the bootloader is polling-only and never enables
 *    an interrupt in the first place.
 *
 * 2. No .data/.bss copy-down. boot/start.S copies initialized globals from
 *    flash to SRAM and zeroes .bss because kernel C code (kernel.c,
 *    shell.c, ...) relies on normal C static/global semantics. boot_main.c
 *    is deliberately written to avoid any initialized-with-a-nonzero-value
 *    or zero-requiring static/global state -- all of its state lives in
 *    local variables (registers/stack) -- specifically so this machinery
 *    can be skipped here, keeping this file (and the flash it costs) as
 *    small as possible against the 1KB Boot Loader Section budget. */

.section .text
.global _start

_start:
    ldi r16, hi8(RAMEND)
    out _SFR_IO_ADDR(SPH), r16
    ldi r16, lo8(RAMEND)
    out _SFR_IO_ADDR(SPL), r16

    clr r1
    out _SFR_IO_ADDR(SREG), r1

    rcall boot_main

hang:
    rjmp hang
