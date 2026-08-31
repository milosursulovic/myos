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

    /* Copy .data from FLASH (LMA) to SRAM (VMA). Empty today (no
     * initialized globals yet), but future milestones will add some —
     * without this loop they'd silently read as flash-offset garbage. */
    ldi r17, hi8(_data_end)
    ldi r26, lo8(_data_start)
    ldi r27, hi8(_data_start)
    ldi r30, lo8(_data_load_start)
    ldi r31, hi8(_data_load_start)
    rjmp copy_data_check
copy_data_loop:
    lpm r0, Z+
    st X+, r0
copy_data_check:
    cpi r26, lo8(_data_end)
    cpc r27, r17
    brne copy_data_loop

    /* Zero .bss. SRAM content is not architecturally guaranteed to be
     * zero at reset (especially after a watchdog reset) — required for
     * correct C semantics for any uninitialized static/global. */
    ldi r17, hi8(_bss_end)
    ldi r26, lo8(_bss_start)
    ldi r27, hi8(_bss_start)
    rjmp zero_bss_check
zero_bss_loop:
    st X+, r1
zero_bss_check:
    cpi r26, lo8(_bss_end)
    cpc r27, r17
    brne zero_bss_loop

    rcall kernel_main

hang:
    rjmp hang
