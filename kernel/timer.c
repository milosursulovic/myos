#include <avr/io.h>
#include <avr/interrupt.h>
#include "timer.h"

/* Incremented once per millisecond by the TIMER0_COMPA ISR. volatile
 * because it's written from interrupt context and read from main-line
 * code; static because only this file's ISR and timer_get_ticks() ever
 * touch it directly. */
static volatile unsigned long system_ticks;

void timer_init(void)
{
    /* Start counting from a known value — don't assume the bootloader
     * (or anything before us) left Timer0 at its power-on-reset default. */
    TCNT0 = 0;

    /* CTC mode (WGM01:WGM00 = 010), TOP = OCR0A. */
    TCCR0A = (1 << WGM01);

    /* Prescaler = 64 (CS02:CS00 = 011). */
    TCCR0B = (1 << CS01) | (1 << CS00);

    /* 16,000,000 / 64 / 250 = 1000 Hz exactly, i.e. a 1ms tick.
     * OCR0A = 249 because CTC counts 0..OCR0A inclusive (250 counts). */
    OCR0A = 249;

    /* Enable the Output Compare A Match interrupt. This is the only
     * interrupt source enabled anywhere in TIMSK0/TIMSK1/TIMSK2 — see
     * boot/start.S's bad_interrupt trap, which relies on that being true. */
    TIMSK0 = (1 << OCIE0A);

    /* Global interrupt enable (sei()) is deliberately NOT done here —
     * that's kernel_main()'s call, made once after all interrupt-driven
     * subsystems are configured. */
}

unsigned long timer_get_ticks(void)
{
    unsigned char sreg;
    unsigned long ticks;

    /* system_ticks is 32 bits, written non-atomically by hardware (the
     * CPU has no 32-bit load/store) — an ISR firing mid-read here could
     * hand back a torn value. Save SREG, disable interrupts, read, then
     * restore SREG (not just re-enable) so this doesn't clobber the
     * caller's interrupt state if it happened to already be disabled. */
    sreg = SREG;
    cli();
    ticks = system_ticks;
    SREG = sreg;

    return ticks;
}

/* Called from kernel/context_switch.S's hand-written TIMER0_COMPA_vect ISR
 * (Milestone 13) -- replaces what used to be a plain ISR(TIMER0_COMPA_vect)
 * here. AVR only allows one handler per vector, and that vector now also
 * has to save/restore task context and drive a scheduler switch (see
 * scheduler_tick() in kernel/scheduler.c), which can't be expressed inside
 * avr-libc's ISR() macro alongside context_switch.S's shared save/restore
 * labels. This function is exactly the old ISR body: increment the tick
 * counter, nothing else. Already running with interrupts disabled (it's
 * called from interrupt context), so the plain increment is safe without
 * its own cli()/SREG dance. */
void timer_tick(void)
{
    system_ticks++;
}
