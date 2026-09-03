#include <avr/io.h>

/* task_yield() / TIMER0_COMPA_vect -- the cooperative AND preemptive
 * context switch, per spec sections 26-27. See docs/scheduler.md for the
 * full design writeup: the register-save-set reasoning, the empirically-
 * verified AVR call/ret stack byte order, how kernel/scheduler.c's fake
 * initial frame (for a task's very first switch-in) stays byte-for-byte
 * symmetric with the restore sequence below, and Milestone 13's
 * reentrancy fix / every-tick preemption policy.
 *
 * Milestone 13 adds a second, independent way a context switch can start:
 * the Timer0 COMPA interrupt, firing at 1kHz (kernel/timer.c), now also
 * switches tasks -- preemptively, whether or not the running task ever
 * calls task_yield(). A task suspended by either mechanism must be
 * resumable by either mechanism later (a task doesn't know or care which
 * one last suspended it), so the 33-byte save and restore sequences live
 * ONCE, as assembler macros (context_save/context_restore below, see
 * their own comment for why macros and not callable subroutines), used
 * by both task_yield and TIMER0_COMPA_vect -- not two hand-copies that
 * could silently drift apart. */

.global task_yield
.global TIMER0_COMPA_vect
.extern scheduler_switch
.extern scheduler_tick

/* --- context_save / context_restore -------------------------------
 * History of four bugs found via hardware testing, all now fixed:
 *
 * Bug 1 (fixed): originally written as `rcall`-able subroutines ending in
 * `ret`. That does NOT work: `context_save` pushes bytes AFTER `rcall
 * context_save` has already pushed its own 2-byte return address, burying
 * it -- the trailing `ret` then pops the last two just-saved register
 * bytes as if they were a return address and jumps to garbage.
 * `context_restore` has the mirror problem. Fixed by making these GNU
 * assembler MACROS instead of callable subroutines: `.macro`/`.endm`
 * text-expand at assembly time, so task_yield and TIMER0_COMPA_vect each
 * get their own literal copy of the instructions (no runtime call/return
 * involved, no stack games) while the sequence still has exactly one
 * source-level definition.
 *
 * Bug 2 (fixed): only saved the 16 AVR-GCC *callee-saved* registers
 * (r2-r17, r28, r29) plus SREG -- correct reasoning for task_yield()
 * ALONE (a task calling it is bound by the normal C ABI, which already
 * guarantees *caller*-saved registers, r0/r1/r18-r27/r30-r31, are never
 * relied on surviving any function call -- task_yield() included). That
 * reasoning does NOT extend to TIMER0_COMPA_vect: a hardware interrupt
 * can land on ANY instruction boundary, including deep inside code that
 * has live values in caller-saved registers with no function-call
 * boundary anywhere nearby -- e.g. uart_puts_P()'s character in r24, or
 * its PROGMEM read pointer in r30:r31 (Z). The interrupted code has no
 * way to know a "call" happened and every reason to expect those
 * registers to survive. Confirmed on real hardware: with only the
 * 16-register save, preemption during banner printing corrupted
 * transmitted bytes, and preemption during uart_getc()'s wait loop
 * eventually left the system unable to ever notice new UART input again.
 * Since a task can be suspended by EITHER task_yield() or a Timer
 * preemption, and doesn't know which, BOTH paths must save/restore the
 * complete, unconditionally-safe set: all 32 general-purpose registers
 * plus SREG (33 bytes) -- not just the 16+1 that would suffice for
 * function-call boundaries alone. */

.macro context_save
    push r0
    in   r0, _SFR_IO_ADDR(SREG)

    /* Bug 3 (fixed, found via hardware testing after bugs 1/2 above):
     * whatever just disabled interrupts before this point -- this macro's
     * caller's own `cli` in task_yield, or the hardware's automatic
     * interrupt-entry clear for TIMER0_COMPA_vect -- already cleared the
     * I-bit, so the SREG value just read into r0 has I=0. Pushing it
     * as-is means every task's saved context claims interrupts were
     * disabled at the moment of suspension, and context_restore's
     * `out SREG, r0` later reproduces that: after the very first switch
     * anywhere in the system, global interrupts never come back on again.
     * Confirmed on real hardware: with this bug, task switching itself
     * kept working forever (it doesn't depend on the global interrupt
     * flag), but NO interrupt -- Timer0 or USART_RX -- ever fired again
     * after the first task_yield() call, so the shell never saw new
     * keystrokes and (before the earlier bug 2 fix) preemption silently
     * stopped happening too.
     *
     * Fix: force the I-bit back on in the copy before pushing it. This is
     * correct, not a hack -- interrupts are only ever disabled for a
     * short critical section (a context switch, or timer_get_ticks()'s
     * atomic read), never as steady state, so whatever the CPU was doing
     * immediately before it got disabled always genuinely had I=1. ORI
     * needs an r16-31 destination, so borrow r16 -- push/pop it right
     * around this use so its real value is back before its own turn in
     * the sequence below; net effect on the stack is zero. */
    push r16
    ldi  r16, 0x80
    or   r0, r16
    pop  r16

    push r0
    push r1

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
    push r18
    push r19
    push r20
    push r21
    push r22
    push r23
    push r24
    push r25
    push r26
    push r27
    push r28
    push r29
    push r30
    push r31
.endm

/* Restores in exact reverse order of context_save above -- 33 bytes: r0,
 * SREG, r1, r2..r31. This symmetry is what makes a genuinely-resumed
 * task's own earlier save unwind correctly, and it is exactly what
 * kernel/scheduler.c's fake frame is laid out to match for a task's
 * first-ever switch-in.
 *
 * r1 is restored to its exact saved value, not force-cleared to 0: under
 * arbitrary-point preemption r1 (AVR-GCC's "always zero between calls"
 * register) can transiently hold a nonzero value mid-sequence (e.g. right
 * after a MUL instruction, before the compiler's own next `clr r1`), and
 * faithfully restoring whatever was actually there is correct -- forcing
 * it to 0 unconditionally would be wrong for that case. (For a genuine
 * task_yield() suspension it's always saved as 0 anyway, since that IS a
 * real function-call boundary where the invariant holds.)
 *
 * Bug 4 (fixed, found via hardware testing after bugs 1-3 above): this
 * macro used to restore SREG's I-bit directly (`out SREG, r0` with r0's
 * saved value, which per Bug 3 always has I=1), then let its caller
 * (task_yield or TIMER0_COMPA_vect) end with a plain `ret`/`reti` a few
 * instructions later. That splits "re-enable interrupts" and "actually
 * hand control back to the resumed task" into two SEPARATE instructions.
 * AVR guarantees only that the ONE instruction immediately after an
 * interrupt-enabling write completes before a pending interrupt is
 * taken -- nothing protects the instruction after that. So the sequence
 * `out SREG,r0` [[grace: pop r0 guaranteed]] `ret`/`reti` has a real gap,
 * one instruction wide, where a Timer0 match that became pending while
 * interrupts were masked (very likely: task_led()/task_system_service()
 * call task_yield() in a tight loop, so task_yield()'s own critical
 * section is entered extremely often, and Timer0 keeps ticking the whole
 * time interrupts are off) gets serviced RIGHT THERE -- before the
 * in-flight switch's own `ret`/`reti` ever pops the resumed task's real
 * return address off its stack.
 *
 * Hand-tracing shows a SINGLE such nested preemption is harmless on its
 * own: the second switch's context_save re-saves the about-to-resume
 * task's own just-restored (correct) register values, and the interrupt
 * hardware's own return-address push lands ABOVE those on the same
 * stack; when that task is eventually resumed for real, popping its
 * re-saved frame lands SP exactly on that pushed address, whose target
 * is itself a `ret`/`reti` -- so control "hops" through it and pops the
 * ORIGINAL pending return address next, resuming correctly. But nothing
 * stops a SECOND (or third, ...) nested preemption from landing in the
 * exact same gap on the very next attempt to resume that same task,
 * before it ever gets to actually run again -- round-robin always moves
 * on to the next task after every switch, raced or not, so a task that
 * keeps getting "sniped" at this gap never gets a chance to unwind, and
 * each snipe permanently adds another 35 bytes (33 register bytes + the
 * hardware's own 2-byte return-address push) to its recorded stack
 * depth. Confirmed on real hardware: with only Bugs 1-3 fixed, the
 * system (Timer0 preemption enabled) resets spontaneously and
 * unpredictably after anywhere from a couple of seconds to several
 * minutes of pure idling (task 1 in uart_getc()'s wait loop, task 2/3
 * looping) -- with Timer0 preemption disabled (task_yield() alone), it
 * never does. task 2 and task 3's 128-byte kmalloc()'d stacks (see
 * kernel/scheduler.c) sit immediately adjacent to the end of .bss (line_
 * buf, then the UART RX ring buffer and rx_head/rx_tail -- see
 * drivers/uart.c), so repeated snipes eventually overflow a task's stack
 * downward into exactly that memory, matching an earlier debug session's
 * observation of uart_getc() reading phantom "received" bytes with no
 * real UART input.
 *
 * Fix: restore SREG in full (as before -- I-bit included, so the other
 * flags land correctly), then immediately `cli` again (one instruction,
 * touches only the I-bit, leaves every other flag exactly as `out SREG`
 * just set it), and make every caller end with `reti` (not `ret`)
 * instead of a plain `ret`. `reti` pops the return address AND sets the
 * I-bit as a SINGLE instruction -- there is no gap between "interrupts
 * become enabled" and "control transfers to the resumed task" for a new
 * interrupt to land in, because they are now the same instruction.
 * (Plain `reti` is legal here even though task_yield() is entered by an
 * ordinary `call`, not a hardware interrupt -- it has no notion of "call
 * depth" or "interrupt nesting count" to get out of sync; it only pops
 * PC and sets I, both of which are exactly what's needed.) Since every
 * saved SREG byte already has I=1 forced in by Bug 3's fix, this
 * `out SREG` / `cli` / ... / `reti` sequence nets out to the identical
 * final SREG value a plain `out SREG` + `ret` would have produced --
 * this changes WHEN interrupts come back on (a few instructions later,
 * atomically with the jump), not what state they end up in.
 *
 * An earlier version of this fix tried to avoid the redundant
 * out-then-immediately-half-undo by ANDing the I-bit out of r0 BEFORE
 * the `out SREG` (borrowing r16 for the AND, the same push/ldi/op/pop
 * pattern Bug 3's fix uses) instead of writing SREG twice. That version
 * assembled to instructions that individually check out against the
 * datasheet, but broke the system on real hardware in a much MORE
 * reproducible way than Bug 4 itself (uart_getc() saw phantom input
 * within the first second of boot, even with Timer0 preemption disabled
 * entirely -- i.e. with no interrupt able to fire during the switch at
 * all, ruling out a timing race and pointing at the AND-based sequence
 * itself). What's specifically wrong with it was not root-caused given
 * time constraints; the plain `out SREG` + `cli` version below was
 * verified correct in the same way (cooperative-only, then preemptive,
 * then extended soak) and doesn't need r16 at all, so it's what shipped
 * -- noted here so a future change back toward the AND-based version
 * doesn't reintroduce a known-bad sequence without re-deriving why. */
.macro context_restore
    pop  r31
    pop  r30
    pop  r29
    pop  r28
    pop  r27
    pop  r26
    pop  r25
    pop  r24
    pop  r23
    pop  r22
    pop  r21
    pop  r20
    pop  r19
    pop  r18
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

    pop  r1
    pop  r0
    out  _SFR_IO_ADDR(SREG), r0
    cli
    pop  r0
.endm

/* --- task_yield() ------------------------------------------------------
 * Called as an ordinary `call task_yield` from C (task_led,
 * task_system_service, uart_getc()'s wait loop -- see kernel/task.c and
 * drivers/uart.c). Voluntarily suspends the calling task and hands off
 * to the next one round-robin.
 *
 * Milestone 13 reentrancy fix: `cli` right at the very start, BEFORE the
 * save begins, and interrupts stay disabled through the entire save +
 * scheduler_switch + SP-install + restore sequence. This used to only
 * protect the tiny SP-install step (Milestone 12), which was safe back
 * when task_yield() was the only thing that could ever touch
 * task_table/current_task_index. That's no longer true now that
 * TIMER0_COMPA_vect can also call into the scheduler: if a Timer
 * preemption fired while task_yield() was mid-save or mid-scheduler_switch
 * (interrupts still enabled up to that point), the two would race on the
 * same shared state -- current_task_index could advance twice, or a
 * task's sp could be recorded incorrectly. Widening the critical section
 * to the whole sequence closes that race by construction: only one
 * context switch can ever be in flight at a time.
 *
 * Interrupts are deliberately never re-enabled explicitly anywhere in
 * this routine -- they come back on ONLY via the trailing `reti` below
 * (see Bug 4's fix comment on context_restore for why it's `reti` and
 * not a plain `ret` after an earlier `out SREG`). This is safe because
 * every task's saved SREG always has the I-bit set: a task can only ever
 * be suspended (by either task_yield or a Timer preemption) from a point
 * where interrupts were genuinely enabled -- task_yield() is never
 * called with interrupts already disabled, and if they had been disabled
 * (e.g. inside kernel/timer.c's timer_get_ticks() own atomic-read cli()
 * section), no interrupt could have fired to preempt it there in the
 * first place. The full critical section is short (33 pushes + a
 * few-line C call + SP swap + 33 pops -- still just a handful of
 * microseconds at 16MHz), negligible for tick accuracy. */
task_yield:
    cli
    context_save

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

    /* --- Install the new task's stack pointer. No separate cli/SREG
     * dance needed around this pair of `out`s anymore (Milestone 12 had
     * one here) -- interrupts are already disabled for the whole routine
     * per the widened critical section above, so nothing can fire
     * between the two writes. */
    out  _SFR_IO_ADDR(SPH), r25
    out  _SFR_IO_ADDR(SPL), r24

    context_restore

    /* `reti`, not `ret` -- see Bug 4's fix comment on context_restore.
     * task_yield() was never entered by a hardware interrupt, but `reti`
     * doesn't require that: it only pops PC and sets the I-bit, both
     * exactly appropriate here, and doing both as ONE instruction (rather
     * than context_restore's `out SREG` a few instructions earlier plus a
     * plain `ret` here) is the whole point of this fix -- it removes the
     * gap a new interrupt could otherwise land in between "interrupts
     * enabled" and "control actually back with the resumed task". */
    reti

/* --- TIMER0_COMPA_vect ---------------------------------------------
 * Preemptive context switch (Milestone 13, spec section 27): fires once
 * per millisecond (see kernel/timer.c's timer_init() -- rate unchanged
 * from Milestone 7/9), and unconditionally switches to the next
 * round-robin task every single time it fires -- no time-slice counter,
 * every tick is a preemption point (see docs/scheduler.md for why).
 *
 * `TIMER0_COMPA_vect` is the macro from <avr/io.h> (already #included)
 * that expands to whatever internal symbol name avr-libc's ISR() macro
 * would otherwise generate -- this is the exact same trick Milestone 9
 * used for USART_RX_vect, so boot/start.S's existing
 * `jmp TIMER0_COMPA_vect` vector-table entry resolves to this hand-written
 * handler with zero changes needed there.
 *
 * No explicit `cli` on entry: the hardware already clears the global
 * interrupt-enable bit the instant this interrupt fires (and pushed the
 * 2-byte return address), before any of our code runs -- the whole ISR
 * body is already a critical section by construction, exactly as
 * task_yield()'s widened one is by its own explicit `cli`. This means at
 * most one context switch (voluntary or preemptive) is ever in flight at
 * once, system-wide. */
TIMER0_COMPA_vect:
    context_save

    /* Same r24:r25 in/out convention as task_yield's call to
     * scheduler_switch above -- scheduler_tick() (kernel/scheduler.c)
     * ticks the system clock (timer_tick()) and then performs the
     * identical round-robin bookkeeping scheduler_switch() does, sharing
     * that logic rather than duplicating it. */
    in   r24, _SFR_IO_ADDR(SPL)
    in   r25, _SFR_IO_ADDR(SPH)
    call scheduler_tick

    out  _SFR_IO_ADDR(SPH), r25
    out  _SFR_IO_ADDR(SPL), r24

    context_restore

    /* `reti` -- both because this is a genuine hardware interrupt entry
     * (the conventionally/architecturally correct instruction to end one
     * with), and, as of Bug 4's fix, because it's now load-bearing:
     * context_restore deliberately leaves the I-bit OFF when it restores
     * SREG, so THIS instruction is what atomically pops the resumed
     * task's return address AND re-enables interrupts in one step -- see
     * Bug 4's fix comment on context_restore for the race this closes. */
    reti
