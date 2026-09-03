#ifndef MYOS_TIMER_H
#define MYOS_TIMER_H

/* Timer0 driver: interrupt-driven 1kHz (1ms) system tick, per spec
 * section 20. Timer0 runs in CTC mode and fires TIMER0_COMPA once per
 * millisecond; the tick counter it drives is exposed to the rest of the
 * kernel/shell via timer_get_ticks().
 *
 * As of Milestone 13 (preemptive scheduler), TIMER0_COMPA_vect is no
 * longer a plain avr-libc ISR() here -- it's hand-written assembly in
 * kernel/context_switch.S (it now also has to save/restore task context
 * and drive a scheduler switch, sharing the exact save/restore code
 * task_yield() uses). timer_tick() is the one piece of that ISR's work
 * that stays plain C: called from the assembly ISR, it does exactly what
 * the old ISR body did (system_ticks++), nothing more. */

void timer_init(void);
unsigned long timer_get_ticks(void);
void timer_tick(void);

#endif /* MYOS_TIMER_H */
