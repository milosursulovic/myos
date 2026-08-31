# Interrupt system — Milestone 8

## Status: infrastructure already built by Milestone 7

Spec section 20 (Timer) draws its own pipeline as interrupt-driven from the
start — `hardware timer -> interrupt -> kernel timer handler ->
system_ticks++` — so Timer already required a real interrupt vector table,
an ISR, and global interrupt enable. That work landed in Milestone 7 (see
`docs/timer.md`), not deferred to this one. This milestone's job is
therefore to name and document that infrastructure as its own concept,
independent of Timer specifically, and confirm nothing is missing before
Milestone 9 (UART interrupts) builds on it. No new code was needed — every
requirement in spec section 21 was already satisfied:

- [x] understand the AVR interrupt vector table — `boot/start.S`
- [x] define interrupt handlers — `ISR()` convention, see below
- [x] enable global interrupts — `sei()` in `kernel/kernel.c`
- [x] write the timer ISR — `kernel/timer.c`
- [ ] UART ISR — explicitly "later" in the spec; that's Milestone 9

## Vector table (`boot/start.S`)

Full detail (byte offsets, why 2-word `jmp` not `rjmp`, why it had to land
in Milestone 7) is in `docs/timer.md`'s "Vector table" section — not
repeated here. The short version: 26 fixed-address slots at the base of
flash, one per interrupt source, each holding a `jmp` to a handler. Every
slot MyOS doesn't use yet jumps to `bad_interrupt` (an infinite self-loop
trap) instead of avr-libc's default silent-reset behavior, so an
unexpected interrupt firing hangs the board conspicuously — easier to
notice on the bench than a mystery reset.

## Adding a new ISR: the established pattern

Milestone 9 (UART RX) and any later interrupt source should follow the
same two-sided pattern Milestone 7 established for Timer0:

1. In `boot/start.S`, change that vector's slot from `jmp bad_interrupt` to
   `jmp <NAME>_vect` — write the `avr/io.h` macro name (e.g.
   `USART_RX_vect`), not a hardcoded `__vector_N` symbol. Because
   `boot/start.S` is preprocessed (`#include <avr/io.h>` already works
   there), the macro expands to whatever internal symbol name avr-libc's
   `ISR()` macro will generate on the C side — the two stay in sync
   automatically, no vector number hardcoded anywhere.
2. In the relevant driver's `.c` file, `#include <avr/interrupt.h>` and
   define `ISR(<NAME>_vect) { ... }`. Keep the body minimal — an ISR should
   do the least work possible (e.g. push a byte into a ring buffer) and
   let main-line code do the rest.
3. Enable that specific interrupt source's enable bit in its own
   peripheral register (e.g. `UCSR0B`'s `RXCIE0` for UART RX) — this can
   happen inside that driver's own `_init()` function, since by the time
   any driver's `_init()` runs, `sei()` hasn't fired yet (see below), so
   there's no window where a half-configured peripheral can interrupt.
4. Global interrupt enable (`sei()`) stays a single, explicit call in
   `kernel_main()`, made once after every interrupt-driven subsystem has
   been initialized — not hidden inside any driver's `_init()`. This was a
   deliberate Milestone 7 decision, not a Milestone 8 one, but it's the
   convention future milestones should keep following.

## Shared state between an ISR and main-line code

Any variable written by an ISR and read by main-line code (or vice versa)
must be `volatile`, and any read/write wider than one byte needs
interrupts masked around it to avoid a torn value — `kernel/timer.c`'s
`timer_get_ticks()` (save `SREG`, `cli()`, read, restore `SREG`) is the
reference pattern; Milestone 9's ring buffer will need the same care for
its read/write indices.

## How to test manually

No new hardware-observable behavior was added in this milestone — it's a
documentation/consolidation checkpoint on top of Milestone 7's working
Timer interrupt. Confirming Milestone 7's `uptime` command still behaves
correctly on real hardware (per `docs/timer.md`'s test steps) is the
practical verification for this milestone too, since it's the same
infrastructure.

**Status:** documentation-only milestone, no code changes. Milestone 7's
hardware test (still pending) covers this infrastructure's actual
behavior.
