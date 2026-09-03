# Scheduler — Milestones 12-13

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

## Explicitly out of scope (Milestone 12)

No preemption (added in Milestone 13 below, reusing this same
context-switch mechanism). No task creation/deletion API. No priority. No
blocking/wake primitives beyond the `uart_getc()` yield-while-waiting
integration above.

---

# Milestone 13 — Preemptive scheduler

Spec section 27 asks for the next step beyond Milestone 12's *cooperative*
switching: **preemptive** switching, where a task gets switched out on a
timer interrupt whether or not it ever calls `task_yield()`. Diagram from
the spec: `Timer -> Interrupt -> Save context -> Scheduler -> Select task
-> Restore context -> RETI`.

## Key design decision: reuse, don't duplicate, Milestone 12's save/restore

The 19-byte register layout a Timer0 interrupt needs to save is *identical*
to what `task_yield()` already saved — a task suspended by either
mechanism must be resumable by either mechanism later; it doesn't know or
care whether it was last suspended voluntarily or by preemption. So the
save and restore byte sequences (previously inline inside `task_yield`)
were factored out into two shared internal labels in
`kernel/context_switch.S`, `context_save` and `context_restore`, reached
via `rcall` (not `call` — both call sites are in the same file, well
within `rcall`'s ±2K-word range) and returning via plain `ret` back to
whichever caller invoked them:

```
context_save:
    in   r0, SREG
    push r0
    push r2 .. push r17        ; 16 registers
    push r28
    push r29                   ; 19 bytes, same as Milestone 12
    ret

context_restore:
    pop  r29
    pop  r28
    pop  r17 .. pop r2          ; exact reverse order
    pop  r0
    out  SREG, r0
    clr  r1
    ret
```

Both `task_yield` and the new `TIMER0_COMPA_vect` ISR `rcall` these same
two labels — verified in the disassembled ELF that both call sites
resolve to the identical `context_save`/`context_restore` addresses, not
two hand-copies that could silently drift apart.

## Required fix to Milestone 12's `task_yield()`: reentrancy

`task_yield()` previously only `cli`-protected the tiny SP-install step —
correct in Milestone 12, where it was the *only* code path that could ever
touch `task_table`/`current_task_index`. That stopped being true the
moment a Timer interrupt can also reach `scheduler_switch()`-equivalent
logic: if a Timer preemption fired *while* `task_yield()` was mid-save
(interrupts were still enabled up to that point) or mid-call into the C
scheduler helper, the two would race on the same shared state —
`current_task_index` could advance twice for one logical switch, or a
task's `sp` could be recorded incorrectly, silently corrupting the task
table.

**Fix:** `task_yield()` now `cli`s immediately on entry — before the
19-byte save begins — and interrupts stay disabled through the *entire*
save + scheduler call + SP-install + restore sequence. They are never
re-enabled explicitly anywhere in `task_yield()`'s own code; they come
back on only via the resumed task's own restored `SREG` byte, inside
`context_restore`'s `out SREG, r0`. This is safe because every task's
saved `SREG` always has the I-bit set: a task can only ever be suspended
(by either mechanism) from a point where interrupts were genuinely
enabled — `task_yield()` is never called with interrupts already
disabled, and if they had been (e.g. inside `timer_get_ticks()`'s own
atomic-read `cli()` section), no interrupt could have fired to preempt it
there in the first place. This closes the race by construction: only one
context switch can ever be in flight system-wide at any instant. The full
critical section is short (~19 pushes + a few-line C call + a 2-`out` SP
swap + ~19 pops — a handful of microseconds at 16MHz), negligible next to
the 1ms tick period.

**Before (Milestone 12)** — `cli` only around the SP-install:

```
    push r29                       ; end of save
    in   r24, SPL
    in   r25, SPH
    call scheduler_switch
    in   r18, SREG                 ; <-- critical section starts here
    cli
    out  SPH, r25
    out  SPL, r24
    out  SREG, r18                 ; <-- critical section ends here
    pop  r29                       ; start of restore, interrupts back on
    ...
```

**After (Milestone 13)** — `cli` at the very top, nothing re-enables it
explicitly:

```
task_yield:
    cli                             ; <-- critical section starts here
    rcall context_save
    in   r24, SPL
    in   r25, SPH
    call scheduler_switch
    out  SPH, r25
    out  SPL, r24
    rcall context_restore           ; restores SREG (I-bit=1) -- interrupts
    ret                             ;     come back on right here
```

The narrower `in r18, SREG` / `out SREG, r18` dance around just the SP
write is gone entirely — it's redundant now that the whole routine is
already inside one `cli`/restored-`SREG` critical section.

## The new `TIMER0_COMPA_vect` ISR

`kernel/timer.c`'s old `ISR(TIMER0_COMPA_vect) { system_ticks++; }` is
gone — a single AVR vector can only have one handler, and this one now
also has to drive a context switch, which can't be expressed inside
avr-libc's `ISR()` macro alongside `context_switch.S`'s shared
save/restore labels. In its place:

- `kernel/timer.c` exports `void timer_tick(void)` — exactly the old ISR
  body (`system_ticks++`), nothing more. `timer_init()`'s
  `TCCR0A`/`TCCR0B`/`OCR0A`/`TIMSK0` setup is byte-for-byte unchanged —
  still the same 1kHz (1ms) rate established in Milestone 7.
- `kernel/context_switch.S` hand-writes the actual `TIMER0_COMPA_vect`
  handler (the macro from `<avr/io.h>`, matching `boot/start.S`'s
  existing `jmp TIMER0_COMPA_vect` vector-table entry with **zero**
  changes needed there — the same symbol-resolution trick Milestone 9
  used for `USART_RX_vect`):

```
TIMER0_COMPA_vect:
    rcall context_save
    in   r24, SPL
    in   r25, SPH
    call scheduler_tick        ; ticks + switches, returns next sp
    out  SPH, r25
    out  SPL, r24
    rcall context_restore
    reti                       ; NOT ret -- genuine interrupt return
```

No explicit `cli` on entry: the hardware already clears the global
interrupt-enable bit the instant the interrupt fires (and pushes the
2-byte return address) before any of this code runs — the ISR body is
already a critical section by construction, exactly as `task_yield`'s
widened one is by its own explicit `cli`. At most one context switch
(voluntary or preemptive) is ever in flight system-wide at once.

## `kernel/scheduler.c`: shared switch bookkeeping

`scheduler_switch()`'s body (record current task's `sp`, mark it `READY`,
round-robin to the next task, mark it `RUNNING`, return its `sp`) was
extracted into a `static` helper, `scheduler_do_switch()`, so the
voluntary and preemptive paths share the exact same bookkeeping instead of
two copies that could drift apart:

```c
static void *scheduler_do_switch(void *current_sp) { /* Milestone 12's body */ }

void *scheduler_switch(void *current_sp)     /* called by task_yield */
{
    return scheduler_do_switch(current_sp);
}

void *scheduler_tick(void *current_sp)       /* called by TIMER0_COMPA_vect */
{
    timer_tick();
    return scheduler_do_switch(current_sp);
}
```

`scheduler_tick()` is reached only from the hand-written ISR in
`kernel/context_switch.S` (via `.extern scheduler_tick` + `call
scheduler_tick`), never from C — like `scheduler_switch()`, it has no
prototype in `scheduler.h`, following the existing convention for
assembly-only-called C helpers in this file. `scheduler_pick_next()`'s
round-robin logic and `task_table` access are completely unchanged from
Milestone 12.

## Preemption policy: every tick, no time-slice counter

Every Timer0 COMPA interrupt (1kHz) is a preemption point: after
`timer_tick()`, `scheduler_tick()` unconditionally advances round-robin to
the next task — no time-slice-length tunable, no per-task counter. The
spec's diagram doesn't describe a slice length, so this is the simplest
complete reading of what it asks for, consistent with the project's
"veoma jednostavan" (very simple) ethos. A real RTOS would typically use a
longer slice to cut context-switch overhead, but at 1kHz with a
few-microsecond switch, overhead here is well under 1% — a known,
accepted simplification, not a defect. `task_yield()` still exists
alongside this for the *voluntary, immediate* case (e.g. `uart_getc()`
giving up the CPU right away instead of waiting up to 1ms for the next
tick) — both mechanisms share the same underlying `context_save` /
`context_restore` / `scheduler_do_switch()` machinery.

## Stack margin under preemption

Milestone 12's 128-byte-per-task budget was sized for *cooperative*
switching, where a task only ever gets suspended at one fixed point
(inside `task_yield()`, called from a known place in its own loop) with a
known, shallow call depth at that instant. Preemption changes that: a
Timer0 tick can now land at *any* instruction boundary in task 2/3's
code, so the real question is the worst-case call depth reachable
anywhere in a task's own execution, not just at its yield point.

Measured (not just assumed) for the current tasks at `-Os`: `task_led()`
keeps its locals entirely in registers (no stack frame of its own), and
its only calls (`timer_get_ticks()`, `gpio_set()`, `gpio_clear()`) are
leaf functions with no frames either — worst case transient stack use
during a preemption inside it is roughly 2 bytes (the interrupted call's
own return address) + 21 bytes (19-byte context save + the 2-byte
hardware-pushed return address) ≈ 23 bytes, against the 128-byte
allocation. `task_system_service()` is lighter still. Comfortable margin
today, but incidental to the current compiler's register allocation and
these two specific simple loops, not a derived bound — a future task with
deeper local call chains would need its own headroom re-checked, not
assumed safe on the strength of "128 bytes is generous."

## Bugs found via hardware testing, after this milestone's initial implementation

The design above was the *initial* Milestone 13 implementation. Hardware
testing (real Arduino Uno, real UART traffic, extended idle periods) found
four real bugs in it, in this order — `kernel/context_switch.S` carries
the full, detailed writeup of each next to the code it fixes; this is a
short summary:

1. **`rcall`-able `context_save`/`context_restore` subroutines don't
   work.** `context_save`'s own `rcall` pushes a 2-byte return address,
   then the macro's later `push`es bury it — its trailing `ret` then pops
   the last two just-*saved register* bytes as if they were a return
   address and jumps to garbage. Fixed by converting both to GNU assembler
   `.macro`/`.endm` — text-expanded inline at both call sites (no runtime
   call/return, no stack games), so the doc's `rcall context_save` /
   `rcall context_restore` snippets above are now historical, not literal.
2. **Only the 16 callee-saved registers (+ `SREG`) were saved, not all
   32 general-purpose registers.** Correct reasoning for `task_yield()`
   *alone* (bound by the normal C ABI), wrong for `TIMER0_COMPA_vect`
   (a hardware interrupt can land on any instruction boundary, including
   deep inside code with live values in caller-saved registers). Confirmed
   on hardware: preemption during banner printing corrupted transmitted
   bytes. Fixed by saving/restoring all 32 GP registers + `SREG` (33
   bytes) in both paths.
3. **The saved `SREG` always had the I-bit clear.** `context_save` read
   `SREG` *after* interrupts were already disabled (by `task_yield`'s own
   `cli`, or the hardware's automatic clear on interrupt entry), so every
   saved context claimed "interrupts were off" — and `context_restore`
   later reproduced that literally. After the first-ever switch anywhere
   in the system, global interrupts never came back on again, so no
   further Timer0 or USART_RX interrupt ever fired. Fixed by forcing the
   I-bit back on in the saved copy before pushing it (correct, not a
   hack — interrupts are only ever disabled for a short critical section,
   never as steady state, so whatever was running immediately before
   always genuinely had them enabled).
4. **A gap between "interrupts re-enabled" and "control actually handed
   to the resumed task."** `context_restore` used to restore the full
   saved `SREG` (I-bit included) via a plain `out SREG, r0`, several
   instructions before its caller's trailing `ret`/`reti`. AVR guarantees
   only that the *one* instruction immediately after an interrupt-enabling
   write completes before a pending interrupt is taken — nothing protects
   the instruction after that. A Timer0 match that became pending while
   interrupts were masked (very likely: `task_led()`/
   `task_system_service()` call `task_yield()` in a tight loop, so its
   critical section is entered extremely often) could be serviced right
   in that gap, before the in-flight switch's own `ret`/`reti` had popped
   the resumed task's real return address. A single such nested
   preemption is self-healing (hand-traced byte-by-byte — see
   `kernel/context_switch.S`), but nothing stops it from recurring on the
   same task before it ever gets to actually resume, and each recurrence
   permanently adds 35 bytes to that task's recorded stack depth — task
   2/3's 128-byte stacks sit immediately below the end of `.bss` (`line_
   buf`, then the UART RX ring buffer and `rx_head`/`rx_tail`), so enough
   recurrences eventually overflow into exactly that memory. Confirmed on
   real hardware: with only bugs 1-3 fixed, the system reset spontaneously
   and unpredictably (anywhere from a couple of seconds to several minutes
   of pure idling) with Timer0 preemption enabled, never with it disabled
   (`task_yield()` alone). Fixed by having `context_restore` restore
   `SREG`'s other flags but leave the I-bit off, and having *every* caller
   (`task_yield()` included, even though it's entered by an ordinary
   `call`, not a hardware interrupt) end with `reti` instead of `ret` —
   `reti` pops the return address and sets the I-bit as a single atomic
   instruction, closing the gap by construction.

None of this changes the byte *count* or *order* Milestone 12's fake
initial frame needs to match (still 33 bytes, still `r0`/`SREG`/`r1`/
`r2..r31`) — only *when*, in the final few instructions, interrupts
actually come back on.

**Status (post-fix): hardware-verified.** With all four bugs fixed, the
exact regression from the "How to test manually" sections below — boot,
`uptime`, 6+ second idle, `uptime` again, full command regression (`help`,
`info`, `gpio 13 on`/`off`, `echo`, `mem`, `tasks`), and an extended idle
period (both a plain multi-minute idle soak and one with `uptime` polled
every ~13 seconds throughout) followed by one more command — passed
repeatedly with correct output and no spontaneous reset, whereas the
pre-fix build reset spontaneously (confirmed via repeated, fresh boot
banners appearing with no host-side reset requested) within seconds to a
few minutes under the same conditions.

## Explicitly out of scope (Milestone 13)

No time-slice tuning/configuration. No priority scheduling. No
preemption-disable API for tasks to protect their own critical sections
beyond what already existed (`timer_get_ticks()`'s internal `cli()` still
works exactly as before, and for the same reason: interrupts fully masked
means no preemption can occur mid-read there either).

## How to test manually — Milestone 13 additions

In addition to Milestone 12's regression list below (still all valid —
every task is now *also* being preempted every tick, on top of whatever
it does voluntarily, and should behave identically from the outside):

1. `make flash PORT=<your port>`.
2. **LED still blinks at the same visible rate** — task 2 is now preempted
   every 1ms tick in addition to its own voluntary `task_yield()` calls;
   externally this should look identical to Milestone 12 (~1Hz, even
   on/off periods).
3. Full shell command regression while the LED runs: `help`, `info`,
   `echo <text>`, `gpio <pin> <on|off>`, `uptime`, `mem`, `tasks`.
4. **`uptime` tick-rate accuracy re-check**: call `uptime`, wait N real
   seconds (stopwatch), call `uptime` again — the tick delta should be
   very close to `N * 1000`, confirming preemption isn't stealing or
   skewing tick counting (each tick still increments exactly once per
   ISR firing, whether or not it also switches tasks).
5. **Extended idle run** (as in Milestone 12, but now a meaningfully
   different stress condition): leave it running for a minute-plus with
   no input, watching for hangs, resets, or corrupted output. Every
   context switch now goes through the *interrupt*-driven path on every
   tick (in addition to the cooperative path task 2/3 already exercised),
   so this exercises the `TIMER0_COMPA_vect` -> `context_save` ->
   `scheduler_tick` -> `context_restore` -> `reti` path hundreds of
   thousands of times, not just the `task_yield` path.
6. **Type under constant 1kHz preemption**: shell responsiveness, echo,
   and backspace should feel identical to Milestone 12 — typing now
   happens while the shell itself is being preempted every millisecond
   between keystrokes, not just when it voluntarily yields.

**Status:** hardware-verified, after 4 rounds of real-hardware bug fixing
(see "Bugs found via hardware testing" above for all four). The initial
implementation described earlier in this section (`.macro`\-free
`rcall`/`ret` subroutines, 19-byte callee-saved-only frames, `SREG`
restored with whatever I-bit it happened to have) never actually passed
hardware testing as-is — each bug was found by running the manual test
steps above (or the extended-idle variant) on a real Arduino Uno and
observing a real failure, not by inspection alone. The current
`kernel/context_switch.S` (33-byte full-register frames, GNU assembler
macros, forced I-bit on save, `reti`-not-`ret` on every exit path) is what
actually passes: the full manual test list above, plus a several-minute
idle soak with `uptime` polled throughout, ran clean with no spontaneous
reset.

Independently re-verified afterward (2026-08-31, same session, different
test harness): fresh flash, `uptime` → 6s idle → `uptime` again (previously
the single most reliable way to reproduce the bug — the second call failed
100% of the time pre-fix) → full command regression (`help`/`info`/
`gpio`/`echo`/`mem`/`tasks`) → 15s idle → one more command, all correct;
then a separate 6-round soak polling `uptime` every ~10s (~70s total)
showed perfectly monotonic, evenly-spaced tick deltas (~11781-11783 ticks
between rounds, consistent to within 2 ticks) with no corruption or
unresponsiveness. LED blink was not re-confirmed visually in this second
pass (no camera access at the time) — the original visual confirmation
predates Milestone 13's bug 3/4 fixes, so a future visual re-check
wouldn't hurt, though nothing in the UART-observable behavior suggests
task 2 is behaving any differently than task 1/3.

---

## How to test manually — Milestone 12 (original)

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
