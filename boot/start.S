#include <avr/io.h>

/* Reset entry must land at flash address 0x0000, so it lives in its own
 * .vectors section, which the linker script places first in .text —
 * independent of object link order. */
.section .vectors
.global _start

/* ATmega328P interrupt vector table. Required as of Milestone 7 (Timer):
 * enabling any interrupt (here, Timer0 Compare Match A) means the hardware
 * can jump to a fixed flash address on interrupt, and until now nothing
 * was placed there — fine with zero interrupt sources enabled, unsafe the
 * moment one is. Each vector is a 2-word (4-byte) `jmp`, not a 1-word
 * `rjmp` — confirmed against actual avr-gcc output for this MCU, not
 * assumed. 26 vectors, byte offsets 0x00-0x64, table ends at 0x68.
 *
 * All vectors except RESET (0x00) and TIMER0_COMPA (0x38) point at
 * `bad_interrupt`. `TIMER0_COMPA_vect` is the macro from <avr/io.h> that
 * expands to whatever internal symbol name avr-libc's `ISR()` macro
 * generates in kernel/timer.c (e.g. `__vector_14`) — writing the macro
 * name here instead of hardcoding that symbol keeps the two sides in sync
 * automatically. */
    jmp _start           /* 0x00 RESET */
    jmp bad_interrupt     /* 0x04 INT0 */
    jmp bad_interrupt     /* 0x08 INT1 */
    jmp bad_interrupt     /* 0x0C PCINT0 */
    jmp bad_interrupt     /* 0x10 PCINT1 */
    jmp bad_interrupt     /* 0x14 PCINT2 */
    jmp bad_interrupt     /* 0x18 WDT */
    jmp bad_interrupt     /* 0x1C TIMER2_COMPA */
    jmp bad_interrupt     /* 0x20 TIMER2_COMPB */
    jmp bad_interrupt     /* 0x24 TIMER2_OVF */
    jmp bad_interrupt     /* 0x28 TIMER1_CAPT */
    jmp bad_interrupt     /* 0x2C TIMER1_COMPA */
    jmp bad_interrupt     /* 0x30 TIMER1_COMPB */
    jmp bad_interrupt     /* 0x34 TIMER1_OVF */
    jmp TIMER0_COMPA_vect /* 0x38 TIMER0_COMPA */
    jmp bad_interrupt     /* 0x3C TIMER0_COMPB */
    jmp bad_interrupt     /* 0x40 TIMER0_OVF */
    jmp bad_interrupt     /* 0x44 SPI_STC */
    jmp bad_interrupt     /* 0x48 USART_RX */
    jmp bad_interrupt     /* 0x4C USART_UDRE */
    jmp bad_interrupt     /* 0x50 USART_TX */
    jmp bad_interrupt     /* 0x54 ADC */
    jmp bad_interrupt     /* 0x58 EE_READY */
    jmp bad_interrupt     /* 0x5C ANALOG_COMP */
    jmp bad_interrupt     /* 0x60 TWI */
    jmp bad_interrupt     /* 0x64 SPM_READY */

/* Trap for any interrupt that should never fire. Only OCIE0A is ever set
 * in TIMSK0 (see kernel/timer.c), so only TIMER0_COMPA is a legitimate
 * interrupt source — if any other vector fires, it's a bug. Hanging here
 * conspicuously (instead of avr-libc's default of silently resetting via
 * an unhandled-vector jump to 0x0000) makes that bug observable rather
 * than hidden. */
bad_interrupt:
    rjmp bad_interrupt

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
