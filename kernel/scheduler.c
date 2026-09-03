#include <stddef.h>
#include "scheduler.h"
#include "kernel/task.h"
#include "kernel/memory.h"
#include "kernel/timer.h"

/* Fixed size for each kmalloc()'d task stack (task 2 and task 3 only --
 * task 1 keeps using the boot stack). 128 bytes is generous given both
 * are simple leaf-ish loops and SRAM has headroom on this project so
 * far; a heuristic, not a tightly derived number, same spirit as
 * kernel/memory.c's SAFETY_MARGIN. */
#define TASK_STACK_SIZE 128

/* Index into the task table of whichever task is currently running.
 * Zero-initialized (task 1, index 0) by .bss, and set explicitly in
 * scheduler_init() for auditability rather than relied on implicitly. */
static unsigned char current_task_index;

/* Internal round-robin picker, used only by scheduler_switch() below
 * (itself only called from kernel/context_switch.S). Deliberately not
 * declared in scheduler.h -- nothing outside this file needs it. */
static task_t *scheduler_pick_next(void)
{
    unsigned char count = task_count();

    current_task_index = (unsigned char)((current_task_index + 1) % count);

    return task_get(current_task_index);
}

/* Builds the fake initial stack frame for a task that has never run yet,
 * so that the FIRST time context_restore (kernel/context_switch.S)
 * switches into it, the trailing `ret`/`reti` jumps straight into
 * t->entry() instead of resuming a real suspended call.
 *
 * Byte layout, from the highest address of the kmalloc()'d block down to
 * the lowest (i.e. in the order a series of real `push` instructions
 * would have left them), must match context_switch.S's
 * context_save/context_restore order exactly in reverse. context_save
 * pushes, in order: r0, SREG (via r0), r1, r2, r3, ..., r31 (33 bytes) --
 * see that macro's own comment for why ALL 32 registers are saved, not
 * just the callee-saved subset a plain function call would need (a Timer
 * preemption can land on any instruction, including code with live
 * values in caller-saved registers). context_restore pops the exact
 * reverse: r31..r2, r1, SREG, r0. So the fake frame, highest address
 * first, is:
 *
 *   [ret addr LOW byte ]   <- highest address (popped LAST, by `ret`)
 *   [ret addr HIGH byte]
 *   [r0 dummy zero byte]   <- context_restore's LAST pop
 *   [SREG = 0x80        ]
 *   [r1 dummy zero byte ]
 *   [r2 .. r31 dummy zero bytes, 30 of them]   <- lowest address (popped FIRST)
 *
 * The two-byte "return address" is deliberately NOT (high, low) in that
 * order -- it is (low, high) here because of how AVR's `call`/`ret`
 * actually place those two bytes on the stack. This was verified
 * empirically (compiled and ran a throwaway probe under qemu-system-avr's
 * cycle-accurate ATmega328P emulation, cross-checked against the ground-
 * truth return address from avr-objdump) rather than assumed -- see
 * docs/scheduler.md for the exact procedure and result: the byte closest
 * to SP (i.e. pushed/popped LAST) is the PC's HIGH byte, and the byte one
 * further down is the PC's LOW byte. AVR-GCC function pointers are
 * already flash WORD addresses (confirmed the same way), so no
 * byte-address-to-word-address shift is needed here. */
static void init_task_stack(task_t *t)
{
    unsigned char *stack = (unsigned char *)kmalloc(TASK_STACK_SIZE);
    unsigned char *sp;
    unsigned int entry_word_addr;
    unsigned char i;

    if (stack == NULL) {
        /* No SRAM available for this task's stack. There is no panic/
         * error-reporting channel yet -- leave t->sp as NULL; a task in
         * this state simply won't be able to run (scheduler_switch()
         * would hand back a NULL sp, which is a pre-existing limitation
         * of this milestone, not something this function can fix). */
        return;
    }

    entry_word_addr = (unsigned int)t->entry;

    /* Work downward from the top of the block, one byte at a time,
     * mirroring exactly what a series of real AVR `push` instructions
     * does (store at the current pointer, then move down) -- see the
     * function comment above for why the byte order below is correct. */
    sp = stack + TASK_STACK_SIZE - 1;

    *sp-- = (unsigned char)(entry_word_addr & 0xFF);        /* ret addr LOW byte  */
    *sp-- = (unsigned char)((entry_word_addr >> 8) & 0xFF);  /* ret addr HIGH byte */

    *sp-- = 0; /* r0 dummy -- context_restore's last pop */

    /* SREG: I-bit (0x80) set, so global interrupts already read as
     * enabled the instant this task starts -- matching the rest of the
     * system after kernel_main()'s one-time sei(). */
    *sp-- = 0x80;

    *sp-- = 0; /* r1 dummy */

    /* r2..r31: 30 dummy bytes -- nothing real to restore on a task's
     * very first switch-in, only the byte COUNT matters. */
    for (i = 0; i < 30; i++) {
        *sp-- = 0;
    }

    t->sp = sp;
}

void scheduler_init(void)
{
    unsigned char count = task_count();
    unsigned char i;

    current_task_index = 0; /* task 1 (index 0) is about to be started */

    /* Task 1 (index 0) needs no allocated stack or fake frame -- it
     * launches directly on the existing boot stack via scheduler_run().
     * Every other task gets a fresh kmalloc()'d stack and a synthesized
     * initial frame. */
    for (i = 1; i < count; i++) {
        task_t *t = task_get(i);

        if (t != NULL) {
            init_task_stack(t);
        }
    }
}

void scheduler_run(void)
{
    task_t *first = task_get(0);

    if (first != NULL && first->entry != NULL) {
        first->state = TASK_STATE_RUNNING;
        first->entry();
    }

    /* first->entry() (shell_run()) never returns, same as before this
     * milestone -- nothing after this point is reachable in practice. */
    for (;;) {
    }
}

/* Shared round-robin switch bookkeeping, used by BOTH the voluntary path
 * (task_yield() -> scheduler_switch(), Milestone 12) and the preemptive
 * path (TIMER0_COMPA_vect -> scheduler_tick(), Milestone 13) -- see
 * kernel/context_switch.S. current_sp is the just-suspended task's
 * post-save stack pointer value; a single pointer-sized argument and
 * pointer-sized return value both travel through r24:r25 (low:high) per
 * the AVR-GCC calling convention -- confirmed empirically against this
 * toolchain (see docs/scheduler.md), not assumed.
 *
 * Records current_sp as the now-suspended task's saved sp, advances to
 * the next task round-robin, and returns that task's saved sp so the
 * assembly can load it straight into the real stack pointer. Extracted
 * as its own function (rather than duplicated in both scheduler_switch()
 * and scheduler_tick()) so the two switch sources can never drift apart. */
static void *scheduler_do_switch(void *current_sp)
{
    unsigned char prev_index = current_task_index;
    task_t *current = task_get(prev_index);
    task_t *next;

    if (current != NULL) {
        current->sp = current_sp;
        current->state = TASK_STATE_READY;
    }

    next = scheduler_pick_next();

    /* Never hand back NULL, or a stack pointer that was never actually
     * allocated (init_task_stack()'s kmalloc() failed), to the assembly --
     * either would get loaded straight into the real SP and corrupt the
     * register file/I-O space on the very next pop. Both cases should be
     * unreachable today (task_count() is fixed and non-zero, and the two
     * 128-byte allocations happen once at boot with ample heap headroom --
     * see init_task_stack()), but if it ever did happen, fall back to
     * keeping the task that was actually still running going, rather than
     * switching into a task that has nowhere valid to resume.
     *
     * Known limitation, not fixed here since it should be unreachable:
     * restoring current_task_index (rather than skipping past the broken
     * slot) means round-robin will land on the same dead task again next
     * time and bail out the same way forever, starving any task ordered
     * after it. Acceptable only because a real kmalloc() failure here
     * shouldn't currently be reachable at all -- if that ever changes,
     * scheduler_pick_next() should skip entries with sp == NULL instead. */
    if (next == NULL || next->sp == NULL) {
        current_task_index = prev_index;
        if (current != NULL) {
            current->state = TASK_STATE_RUNNING;
        }
        return current_sp;
    }

    next->state = TASK_STATE_RUNNING;

    return next->sp;
}

/* Called from kernel/context_switch.S's task_yield -- the voluntary
 * switch path (Milestone 12), unchanged in behavior by Milestone 13's
 * preemption work beyond now sharing scheduler_do_switch() above with
 * scheduler_tick(). */
void *scheduler_switch(void *current_sp)
{
    return scheduler_do_switch(current_sp);
}

/* Called from kernel/context_switch.S's TIMER0_COMPA_vect ISR only --
 * the preemptive switch path (Milestone 13). Runs with interrupts
 * disabled (interrupt context), after the ISR has already pushed the
 * same 19 callee-saved bytes task_yield() pushes.
 *
 * Every-tick preemption policy: ticks the system clock, then always
 * performs the same round-robin advance task_yield() would -- no
 * separate time-slice counter (see docs/scheduler.md for why this is
 * the simplest faithful reading of the spec's Timer -> Interrupt -> Save
 * context -> Scheduler -> Select task -> Restore context -> RETI
 * diagram). */
void *scheduler_tick(void *current_sp)
{
    timer_tick();
    return scheduler_do_switch(current_sp);
}
