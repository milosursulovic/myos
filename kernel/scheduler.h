#ifndef MYOS_SCHEDULER_H
#define MYOS_SCHEDULER_H

/* Cooperative round-robin scheduler, per spec section 26. See
 * docs/scheduler.md for the full design: the task_t.sp extension, the
 * register-save-set reasoning, the empirically-verified call/ret stack
 * byte order the fake initial frame relies on, and the kmalloc()-based
 * per-task stacks.
 *
 * task_yield() is declared here (its implementation lives entirely in
 * kernel/context_switch.S, hand-written AVR assembly) since it's the
 * scheduler-facing primitive every task's C code -- and now
 * drivers/uart.c's uart_getc() -- calls to give up the CPU. */

/* Allocates a kmalloc()'d stack and a synthesized initial frame for every
 * task except task 1 (the shell keeps running on the existing boot
 * stack). Must be called once, after task_init() has populated the task
 * table and before scheduler_run(). */
void scheduler_init(void);

/* Starts task 1 (the shell) by calling its entry point directly, as an
 * ordinary C call on the current (boot) stack -- no assembly involved in
 * this first launch. Control only ever leaves this call via a
 * task_yield() deep inside task 1's own call chain, which hands off to
 * task 2/3 via context_switch.S. Never returns, mirroring shell_run()
 * never returning. */
void scheduler_run(void);

/* Suspends the calling task and hands control to the next task
 * (round-robin), resuming exactly where this task left off once the
 * scheduler switches back to it. Implemented in kernel/context_switch.S
 * -- see docs/scheduler.md for the register-save-set and stack-frame
 * design. */
void task_yield(void);

#endif /* MYOS_SCHEDULER_H */
