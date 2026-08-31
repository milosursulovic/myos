# Scheduler — Milestone 12

## What it does

`kernel/scheduler.h` / `kernel/scheduler.c` / `kernel/context_switch.S`
turn Milestone 11's static task table into genuine cooperative
multitasking, per spec section 26: `scheduler_init()`, `scheduler_run()`,
and `task_yield()`. Each task now keeps its own saved stack pointer so
`task_yield()` can suspend the calling task and resume a *different* one
exactly where it last left off — task 2 (`task_led`) actually blinks in
the background while task 1 (the shell) sits idle at the prompt, and
task 3 (`task_system_service`) round-robins in alongside them.

This required hand-written AVR context-switch assembly
(`kernel/context_switch.S`), comparable in risk/precision to
`boot/start.S`'s vector table work in Milestone 7.

## `task_t` extension

The spec's `task_t` only lists `id`/`state`/`entry`, but a task cannot be
suspended and later resumed without somewhere to save its stack pointer
between switches:

```c
#define TASK_STATE_READY   1
#define TASK_STATE_RUNNING 2

typedef struct {
    unsigned char id;
    unsigned char state;
    void (*entry)(void);
    void *sp;    /* saved stack pointer while not running; NEW in Milestone 12 */
} task_t;
```

`task_get()` now returns `task_t *` instead of `const task_t *`, since the
scheduler needs to mutate `sp` and `state` through it; `shell.c`'s
existing read-only usage is unaffected by dropping `const`.

`TASK_STATE_RUNNING` is new too (optional per the milestone plan, added
because it's cheap and now genuinely meaningful): `kernel/scheduler.c`
marks whichever task is currently executing `RUNNING` and every other task
`READY` on each switch, so the shell's `tasks` command shows which task is
actually active.

Task 1 (the shell) keeps using the existing boot stack it's already
running on — no allocation or fake frame needed for it. Only task 2 and
task 3 get `kmalloc()`'d stacks with a synthesized initial frame.

## Register save/restore set

`task_yield()` is called as an ordinary `call task_yield` from C. Per the
AVR-GCC calling convention, the caller-saved registers (`r0`, `r18`-`r27`,
`r30`-`r31`) need no preservation — the ABI never guarantees they survive
*any* function call, so no task's C code can be relying on that. Only the
**callee-saved** registers are saved/restored, since some caller's live
local variable could be sitting in one of them:

- `r2`-`r17` (16 registers)
- `r28`/`r29` (the Y pointer, 2 registers)
- `SREG` (1 byte) — saved via `r0` as scratch (there's no direct
  push-SREG instruction) — cheap insurance, matches common reference
  cooperative-kernel implementations
- `r1` is excluded entirely — it's AVR-GCC's permanent "always zero"
  register, not per-task state. `context_switch.S` explicitly `clr r1`
  right before its final `ret` as belt-and-braces insurance (nothing in
  the routine actually touches it, but this is the one place arbitrary
  task code hands control to other arbitrary task code).

19 bytes total per suspended task, sitting on top of the 2-byte return
address the `call task_yield` instruction auto-pushed.

## Empirically-verified AVR call/ret stack byte order

Getting the fake initial frame's return-address bytes in the wrong order
would send task 2/3 to a garbage address on their first switch-in — silent
and hard to diagnose. This was **not** derived from memory; it was
verified by compiling and running a throwaway probe program under
`qemu-system-avr`'s `-M uno` machine (a cycle/instruction-accurate
ATmega328P emulation), cross-checked against ground truth from
`avr-objdump`.

Procedure:
1. Wrote a tiny hand-assembled program (`_start`, no vector table needed
   since no interrupts are enabled) that does `call target`, where
   `target:` reads the two bytes sitting at `[SP+1]` and `[SP+2]`
   (immediately after entering `target`, without disturbing SP) using a Z
   pointer built from `SPL`/`SPH`, and transmits them raw over UART.
2. Disassembled the built ELF with `avr-objdump -d` to get the *actual*
   flash byte address of the instruction right after `call target`
   (`after_call`, at byte address `0x28`). AVR's PC is word-addressed, so
   the value `call` pushes as the return address is the **word** address:
   `0x28 / 2 = 0x0014` (high byte `0x00`, low byte `0x14`).
3. Ran the ELF under `qemu-system-avr -M uno -nographic -serial file:out.bin`
   and inspected the captured bytes with `xxd`: the two transmitted bytes
   were `0x00` then `0x14` — i.e. `mem[SP+1] = 0x00` (high byte) and
   `mem[SP+2] = 0x14` (low byte).

**Result: the byte at `[SP+1]` (closest to the current SP, "top of
stack") is the return address's HIGH byte; the byte at `[SP+2]` is the
LOW byte.** Equivalently: `call` pushes the low byte first (ends up
farther from SP) and the high byte second (ends up closest to SP,
following AVR's store-then-decrement `push` semantics, where SP always
points to the *next free* byte and the most recently pushed byte sits at
`SP+1`).

A second, related fact was checked the same way (compile + disassemble a
tiny C program with a function pointer, `docs/scheduler.md`'s
implementation notes): **AVR-GCC represents C function pointers as flash
WORD addresses already** (`(unsigned int)some_fn` equals `byte_address /
2`, confirmed against real compiler output), so no extra shift is needed
when building the fake return address from `task->entry`.

## Fake initial frame for task 2 / task 3

`scheduler_init()` `kmalloc()`s a 128-byte stack for each of task 2 and
task 3 (a heuristic like `kernel/memory.c`'s `SAFETY_MARGIN`, not a
tightly derived number — generous given both are simple leaf-ish
functions and SRAM has headroom). `kernel/scheduler.c`'s
`init_task_stack()` then hand-constructs, at the top of that block, a byte
layout matching exactly what `task_yield()`'s restore sequence expects to
pop, built downward one byte at a time exactly like a series of real
`push` instructions would leave it:

```
[ret addr LOW byte ]   <- highest address (popped LAST, by `ret`)
[ret addr HIGH byte]
[SREG = 0x80        ]
[18 dummy zero bytes]  (stand-ins for r17..r2, r28, r29)
                        <- task->sp points here (lowest address, one below
                           the lowest occupied byte, "next free" per AVR's
                           SP convention)
```

`SREG`'s synthesized value is `0x80` (I-bit set) so global interrupts
already read as enabled the moment the task starts, matching the rest of
the system after `kernel_main()`'s one-time `sei()`. The 18 dummy register
bytes are zero — nothing real to restore yet, only the byte *count*
matters for the pop sequence to land correctly. The return address is
`task->entry`'s word address, split (low byte, high byte) per the
empirically-verified order above, so `context_switch.S`'s final `ret`
jumps straight into `task_led`/`task_system_service` the first time each
task is switched into.

## `context_switch.S` — `task_yield()`

```
task_yield:
    in   r0, SREG
    push r0
    push r2 .. push r17        ; 16 registers
    push r28
    push r29                   ; 19 bytes saved total

    in   r24, SPL
    in   r25, SPH
    call scheduler_switch      ; C helper -- see below, returns next sp in r24:r25

    ; SPH/SPL write is cli-protected (two `out`s to a live hardware
    ; register every interrupt depends on) -- same critical-section
    ; pattern as kernel/timer.c's timer_get_ticks()
    in   r18, SREG
    cli
    out  SPH, r25
    out  SPL, r24
    out  SREG, r18

    pop  r29
    pop  r28
    pop  r17 .. pop r2          ; exact reverse order of the saves
    pop  r0
    out  SREG, r0

    clr  r1                     ; never leave the zero register non-zero
    ret
```

The register-set save/restore, the SP capture/install, and the final
`ret` are all done in assembly; everything else (recording where a task's
stack sits, picking the next task, updating `state`) is delegated to a
small C helper, `scheduler_switch()`, to minimize hand-written assembly:

```c
void *scheduler_switch(void *current_sp)
{
    task_t *current = task_get(current_task_index);
    task_t *next;

    if (current != NULL) {
        current->sp = current_sp;
        current->state = TASK_STATE_READY;
    }

    next = scheduler_pick_next();   /* round-robin, wraps at task_count() */

    next->state = TASK_STATE_RUNNING;
    return next->sp;
}
```

The single pointer-sized argument/return value travel through `r24:r25`
(low:high) — the AVR-GCC calling convention for a lone <=16-bit
parameter/return, confirmed empirically against this project's actual
toolchain (a throwaway `void *helper(void *x)` compiled and disassembled)
rather than assumed.

## `kmalloc()` — first real caller

`scheduler_init()` is the first real caller of Milestone 10's allocator:
two 128-byte allocations (task 2 and task 3's stacks), well within
`kmalloc()`'s `MAX_ALLOC_SIZE` (1024) and the 2KB SRAM budget.

## `uart_getc()` yield integration

`drivers/uart.c`'s `uart_getc()` wait loop now calls `task_yield()` every
iteration instead of pure busy-spin:

```c
while (rx_head == rx_tail) {
    task_yield();
}
```

Without this, task 1 (the shell) would hog the CPU forever the moment it
blocks waiting for a keystroke, and task 2/3 would never run — this is
what makes the milestone's headline demo (a live background LED task next
to an interactive shell) actually work. It's safe: `rx_head`/`rx_tail` are
re-read fresh on every loop iteration after control returns from
`task_yield()` (a full round trip through every other task and back),
exactly as if this were still a plain busy-spin — yielding only changes
who else gets to run while blocked, not what condition is being checked
or how. This adds a deliberate `drivers/uart.c` -> `kernel/scheduler.h`
dependency, normal and expected for a cooperative kernel's blocking I/O
primitives.

## Wiring

- `kernel/kernel.c`: `shell_run()` is replaced with
  `scheduler_init(); scheduler_run();`.
- `kernel/task.c`: `task_led()` calls `task_yield()` on iterations where
  it isn't yet time to toggle the LED; `task_system_service()` calls
  `task_yield()` every iteration (it has nothing to do yet).

## Explicitly out of scope

No preemption (Milestone 13 — timer-interrupt-driven switching, reusing
this same context-switch mechanism). No task creation/deletion API. No
priority. No blocking/wake primitives beyond the `uart_getc()`
yield-while-waiting integration above.

## How to test manually

1. `make flash PORT=<your port>`.
2. Connect a serial terminal at 9600 8N1.
3. **Core demonstration**: with no input typed, watch pin 13's onboard
   LED — it should blink at roughly 1Hz (500 ticks / ~500ms on, ~500ms
   off) *while the shell sits idle at the `myos>` prompt*.
4. Full command regression while the LED keeps blinking: `help`, `info`,
   `echo <text>`, `gpio <pin> <on|off>`, `uptime`, `mem`, `tasks` (expect
   task 1 shown `RUNNING`, tasks 2/3 `READY` most of the time you happen
   to check, since the shell is what's usually waiting on your input).
5. Let it run for a minute or more, watching for any hang, reset, or
   corrupted output — stack-switch bugs can manifest as slow corruption
   rather than an immediate crash.
6. `gpio 13 on` / `gpio 13 off` while the LED task is also actively
   driving pin 13 — expected to visibly *fight* with the LED task's own
   toggling (both are legitimately driving the same pin at the same time
   now that they run concurrently). This is a real, expected interaction
   of running two independent drivers of the same pin, not a bug.

**Status:** hardware-verified (2026-08-31). The onboard LED was visually
confirmed blinking evenly at the ~1Hz period while the shell sat idle at
`myos>` with no input. Objectively, over a 40+ second idle stretch (during
which `task_system_service()` and `task_led()` were each calling
`task_yield()` on effectively every loop iteration — likely hundreds of
thousands of successful context switches), the shell remained fully
responsive and correct: `uptime`/`tasks`/`help`/`info`/`echo`/`mem` all
produced exactly the expected output afterward, `tasks` correctly showed
task 1 `RUNNING` and tasks 2/3 `READY`, `uptime`'s tick count tracked
wall-clock time correctly (52140 ticks after ~52s), and `gpio 13 on`/`off`
worked correctly even with the LED task concurrently driving the same
pin. No hang, reset, or corruption observed.

Found during review, fixed before hardware testing: `scheduler_switch()`
originally had no guard against a task whose `kmalloc()`'d stack
allocation had failed (`sp == NULL`) — round-robin would still advance
onto it and the assembly would install a null/zero stack pointer,
corrupting the register file. Currently unreachable in practice (the two
128-byte allocations happen once at boot with ample heap headroom), but
fixed defensively regardless, with a documented known limitation: the
fallback restores `current_task_index` rather than skipping past the
broken slot, so a real allocation failure would still starve any task
ordered after it — acceptable only because that failure mode itself
shouldn't be reachable today.
