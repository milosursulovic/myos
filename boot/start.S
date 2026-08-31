#include <avr/io.h>

/* Reset entry must land at flash address 0x0000, so it lives in its own
 * .vectors section, which the linker script places first in .text —
 * independent of object link order. */
.section .vectors
.global _start

_start:
    ldi r16, hi8(RAMEND)
    out _SFR_IO_ADDR(SPH), r16
    ldi r16, lo8(RAMEND)
    out _SFR_IO_ADDR(SPL), r16

    clr r1
    out _SFR_IO_ADDR(SREG), r1

    rcall kernel_main

hang:
    rjmp hang
