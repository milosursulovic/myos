#include <avr/io.h>

/* task_yield() -- the cooperative context switch, per spec section 26.
 * See docs/scheduler.md for the full design writeup: the register-save-
 * set reasoning, the empirically-verified AVR call/ret stack byte order,
 * and how kernel/scheduler.c's fake initial frame (for a task's very
 * first switch-in) stays byte-for-byte symmetric with the restore
 * sequence below.
 *
 * Called as an ordinary `call task_yield` from C (task_led,
 * task_system_service, uart_getc()'s wait loop -- see kernel/task.c and
 * drivers/uart.c). Saves the callee-saved registers onto the CURRENT
 * task's stack, hands off to a small C helper (kernel/scheduler.c's
 * scheduler_switch()) which records where this task's stack now sits and
 * picks the next task round-robin, swaps the CPU's real stack pointer
 * over to that task's saved stack, restores its saved registers, and
 * `ret`s -- landing either back inside whatever C code that task last
 * called task_yield() from (a genuine resume), or, the first time a
 * freshly-created task (2 or 3) is switched into, straight into its
 * entry() function via scheduler_init()'s fake frame. */

.global task_yield
.extern scheduler_switch

task_yield:
    /* --- Save SREG and all 16 callee-saved general registers plus the Y
     * pointer (r28/r29) onto the CURRENT task's stack: 19 bytes total,
     * on top of the 2-byte return address `call task_yield` already
     * pushed. Per the AVR-GCC calling convention, these are exactly the
     * registers a caller's live C locals could be sitting in across an
     * ordinary function call -- r0/r1 and the caller-saved registers
     * (r18-r27, r30/r31) are never guaranteed to survive a `call`, so no
     * task's code can be relying on them here and they are deliberately
     * left untouched. SREG has no direct push instruction, so it goes
     * through r0 (caller-saved, safe scratch) first. */
    in   r0, _SFR_IO_ADDR(SREG)
    push r0

    push r2
    push r3
    push r4
    push r5
    push r6
    push r7
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    push r16
    push r17

    push r28
    push r29

    /* --- Hand the post-save SP to the C helper, which records it as
     * this task's saved sp, advances to the next task (round-robin), and
     * returns THAT task's saved sp. A single pointer-sized argument and
     * pointer-sized return value both travel through r24:r25 (low:high)
     * -- the AVR-GCC calling convention for a lone <=16-bit parameter/
     * return, confirmed empirically against this exact toolchain rather
     * than assumed (see docs/scheduler.md). */
    in   r24, _SFR_IO_ADDR(SPL)
    in   r25, _SFR_IO_ADDR(SPH)
    call scheduler_switch

    /* --- Install the new task's stack pointer. Writing SPH/SPL is two
     * separate `out`s to a live hardware register that every interrupt
     * implicitly depends on; if TIMER0_COMPA or USART_RX fired between
     * them, that ISR would push its own return address using a half-old/
     * half-new SP, corrupting whichever task's stack it landed on.
     * Save/disable/restore SREG around the pair, same critical-section
     * pattern as kernel/timer.c's timer_get_ticks(). (Reading the OLD sp
     * above needed no such protection: nothing we do before this point
     * writes SP, and a well-formed ISR always leaves SP exactly as it
     * found it by the time it returns.) */
    in   r18, _SFR_IO_ADDR(SREG)
    cli
    out  _SFR_IO_ADDR(SPH), r25
    out  _SFR_IO_ADDR(SPL), r24
    out  _SFR_IO_ADDR(SREG), r18

    /* --- Restore in exact reverse order of the saves above. This
     * symmetry is what makes a genuinely-resumed task's own earlier save
     * unwind correctly, and it is exactly what kernel/scheduler.c's fake
     * frame is laid out to match for a task's first-ever switch-in. */
    pop  r29
    pop  r28

    pop  r17
    pop  r16
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  r7
    pop  r6
    pop  r5
    pop  r4
    pop  r3
    pop  r2

    pop  r0
    out  _SFR_IO_ADDR(SREG), r0

    /* r1 is AVR-GCC's permanent "always zero" register, not per-task
     * state -- deliberately excluded from the saved set above. Nothing
     * in this routine touches it, so it should already read as 0 here,
     * but this switch is the one place arbitrary task code hands control
     * to other arbitrary task code, so clear it explicitly as cheap
     * insurance rather than trust that invariant silently held. */
    clr  r1

    ret
