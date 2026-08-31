#ifndef MYOS_TIMER_H
#define MYOS_TIMER_H

/* Timer0 driver: interrupt-driven 1kHz (1ms) system tick, per spec
 * section 20. Timer0 runs in CTC mode and fires TIMER0_COMPA once per
 * millisecond; the ISR increments a tick counter that timer_get_ticks()
 * exposes to the rest of the kernel/shell. */

void timer_init(void);
unsigned long timer_get_ticks(void);

#endif /* MYOS_TIMER_H */
