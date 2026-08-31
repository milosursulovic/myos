# Timer — Milestone 7

## What it does

`kernel/timer.c` / `kernel/timer.h` implement an interrupt-driven 1kHz
system tick using Timer0, per spec section 20: hardware timer -> interrupt
-> kernel timer handler -> `system_ticks++`. `shell/shell.c` gets an
`uptime` command that reports the tick count and elapsed seconds.

This is the project's first use of an AVR interrupt, which required adding
a real interrupt vector table to `boot/start.S` (previously execution just
started directly at flash address 0 with nothing else there — safe with
zero interrupt sources enabled, unsafe the moment one is enabled, since the
hardware jumps to a fixed flash address on interrupt).

## Vector table (`boot/start.S`)

The ATmega328P vector table uses 2-word (4-byte) `jmp` instructions per
slot — not the 1-word `rjmp`/2-byte spacing some other AVR parts use — with
26 vectors spanning byte offsets `0x00`-`0x64` (table ends at `0x68`).
Confirmed against actual `avr-gcc`/`avr-objdump` output for this MCU, not
assumed. `0x00` (RESET) jumps to `_start`; `0x38` (TIMER0_COMPA) jumps to
`TIMER0_COMPA_vect`, a macro from `<avr/io.h>` that expands (via
`avr/iom328p.h`) to whatever internal symbol name avr-libc's `ISR()` macro
generates for that vector in `kernel/timer.c` — this keeps the assembly
side and the C side in sync without hardcoding a vector number or symbol
name anywhere. Every other slot jumps to `bad_interrupt`, an infinite
self-loop trap placed right after the table: only `OCIE0A` is ever set in
`TIMSK0`, so no other interrupt should ever legitimately fire — if one
does (a bug), hanging conspicuously here is more useful for debugging than
avr-libc's default behavior of silently resetting via an unhandled-vector
jump to `0x0000`.

The table, `bad_interrupt`, and the existing `_start` body (SP init,
`.data` copy, `.bss` zero, `rcall kernel_main`) all stay in the same
`.section .vectors`, so `linker.ld`'s existing `*(.vectors)` rule places
the whole thing first in flash with no linker script changes. `_start`'s
body is unchanged — it's now reached via `jmp _start` from the table
instead of being at address 0 directly.

Global interrupts are **not** enabled in `_start` — `sei()` is a
kernel-level policy decision (see below), not a runtime-init detail.

## 1kHz tick derivation

Timer0 runs in CTC mode (`WGM01` set, `TOP = OCR0A`) with a /64 prescaler
(`CS01`, `CS00` set):

```
F_CPU / prescaler / (OCR0A + 1) = 16,000,000 / 64 / 250 = 1000 Hz
```

so `OCR0A = 249` (CTC counts `0..OCR0A` inclusive, 250 counts per period)
gives an exact 1ms tick — no rounding error. `TIMSK0 = (1 << OCIE0A)`
enables the Output Compare A Match interrupt; `TCCR0A`/`TCCR0B` are the two
mode/clock-select registers per the datasheet Timer/Counter0 section.

## API

```c
void timer_init(void);              // configure Timer0 for a 1kHz tick (does not call sei())
unsigned long timer_get_ticks(void); // milliseconds since timer_init(), atomically read
```

`ISR(TIMER0_COMPA_vect) { system_ticks++; }` in `kernel/timer.c` is the
handler; `system_ticks` is a file-local `static volatile unsigned long`.

### Atomic read

`system_ticks` is 32 bits on an 8-bit CPU, so a plain read is several
instructions and not atomic — an ISR firing mid-read could hand back a
torn value (e.g. high two bytes updated, low two not yet, or vice versa).
`timer_get_ticks()` guards the read by hand: save `SREG`, `cli()`, read
`system_ticks` into a local, restore `SREG` (not just re-enable
interrupts, so this doesn't clobber a caller that already had interrupts
disabled), then return the local.

## Kernel wiring (`kernel/kernel.c`)

```c
uart_init();
gpio_init();
timer_init();
sei();          // the one place global interrupts are enabled
shell_run();
```

`sei()` is called explicitly in `kernel_main()`, as its own visible step,
after every interrupt-driven subsystem is configured — not hidden inside
`timer_init()` or any other driver's init function.

## Shell command

```
myos> uptime
Ticks: 12345
Uptime: 12 seconds
```

`Uptime` is `ticks / 1000`, truncating (matches the 1kHz tick rate
exactly — the spec's own example: 12345 ticks -> 12 seconds).

## How to test manually

1. `make flash PORT=<your port>`.
2. Connect a serial terminal at 9600 8N1.
3. Type `uptime`, wait a few seconds (e.g. count roughly 5 seconds
   yourself), then type `uptime` again — the tick count should have
   increased by approximately 1000 per second of wall-clock time between
   the two calls, and `Uptime:` should have increased by the same number
   of seconds.
4. Type `help` — expect `uptime` listed alongside `help`, `info`, `echo`,
   `gpio`.
5. General regression check: `gpio 13 on`/`off` and `echo` should still
   work as before — enabling the timer interrupt shouldn't disturb
   anything else.

**Status:** build-verified only (clean `make`, disassembly-verified vector
table). Hardware test pending (needs a physical Uno) — required before
this milestone counts as done per `CLAUDE.md`.
