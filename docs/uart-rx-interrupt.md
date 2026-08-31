# UART RX interrupt — Milestone 9

## What it does

`drivers/uart.c` converts UART receive from polling (Milestone 2) to
interrupt-driven, per spec section 22's pipeline:

```
UART RX interrupt -> ISR -> receive buffer -> shell
```

Scope is RX only, matching the spec diagram exactly. TX
(`uart_putc()`/`uart_puts()`/`uart_puts_P()`/`uart_put_uint()`/
`uart_put_ulong()`) is unchanged — still polling on `UDRE0`. `drivers/uart.h`'s
public API is unchanged; this is an implementation swap behind
`uart_getc()`'s existing blocking contract, so `shell/shell.c`'s
`read_line()` needed no changes at all.

## Ring buffer design

A 16-byte buffer (`RX_BUF_SIZE`, a power of 2) with `rx_head`/`rx_tail`
indices:

```c
static volatile unsigned char rx_buf[RX_BUF_SIZE];
static volatile unsigned char rx_head; /* next slot the ISR will write */
static volatile unsigned char rx_tail; /* next slot uart_getc() will read */
```

Wraparound uses `& (RX_BUF_SIZE - 1)` instead of `% RX_BUF_SIZE`. That
bitmask trick is only equivalent to modulo when the divisor is a power of
2 (masking off the high bits discards exactly one full period) — if
`RX_BUF_SIZE` is ever changed, it must stay a power of 2 or the mask breaks
silently. 16 bytes is plenty: it only needs to smooth over the few
characters typed between two `uart_getc()` calls from the shell, and keeps
the SRAM footprint small on a 2KB part.

### Why no locking is needed

`ISR(USART_RX_vect)` (the producer) writes `rx_head`; `uart_getc()` (the
consumer) reads and advances `rx_tail`. This is a classic single-producer/
single-consumer lock-free ring buffer. Unlike `kernel/timer.c`'s
`system_ticks` (a 32-bit value needing `cli()`/`SREG` masking around its
read because a multi-byte access can be torn by an interrupt landing
mid-read), `rx_head` and `rx_tail` are each a single `unsigned char`. On an
8-bit AVR a single-byte load or store is one instruction, so it can't be
observed half-written — no masking is needed to read/write either index
safely from either side.

### Overflow policy

`ISR(USART_RX_vect)` always reads `UDR0` first, even if the buffer is
full — that read is what clears the `RXC0` flag, and skipping it would
leave the interrupt permanently pending (the ISR would fire again
immediately, forever). If the buffer is full (`(rx_head + 1) & RX_BUF_MASK
== rx_tail`), the byte is dropped silently after being read. There is no
channel to report an overflow back to the sender, and an ISR cannot block
waiting for `uart_getc()` to free a slot — dropping is the only option.
Under normal interactive typing (a human typing well under the buffer's
16-byte capacity between shell reads) this should never trigger.

## Vector table (`boot/start.S`)

The `USART_RX` slot (byte offset `0x48`) changes from `jmp bad_interrupt`
to `jmp USART_RX_vect`, following the same pattern Milestone 7 established
for `TIMER0_COMPA` — `USART_RX_vect` is the `<avr/io.h>` macro (expands to
vector 18 / `__vector_18`) rather than a hardcoded symbol, so the assembly
and C sides stay in sync automatically. `bad_interrupt`'s comment is
updated to note both legitimate interrupt sources (`OCIE0A` in `TIMSK0`,
`RXCIE0` in `UCSR0B`) instead of just Timer0.

## API (unchanged)

```c
void uart_init(void);                 // now also enables RXCIE0
void uart_putc(char c);               // unchanged, still polls UDRE0
void uart_puts(const char *str);      // unchanged
void uart_puts_P(const char *str);    // unchanged
char uart_getc(void);                 // same blocking contract, now reads from the ring buffer
void uart_put_uint(unsigned int n);   // unchanged
void uart_put_ulong(unsigned long n); // unchanged
```

`uart_init()` adds `RXCIE0` to the existing `UCSR0B` write (alongside
`RXEN0`/`TXEN0`) to enable the USART RX Complete interrupt. No new `sei()`
call was added anywhere — global interrupt enable is still the single call
in `kernel_main()` (`kernel/kernel.c`), made after `uart_init()` and
`timer_init()` both run, so there's no window where a half-configured
peripheral can interrupt.

## Note on docs/uart.md and docs/uart-echo.md

Those two documents describe the Milestone 2/4 polling-era RX behavior and
were deliberately left untouched by this milestone. The receive path they
describe is now interrupt-driven internally, but the externally observable
behavior (`uart_getc()` blocks until a byte is available, returns it) is
identical, so nothing in them is actually inaccurate — no correction is
needed there.

## How to test manually

1. `make flash PORT=<your port>`.
2. Connect a serial terminal at 9600 8N1.
3. Type quickly, or paste several characters at once (a burst larger than
   a single UART frame time but well under 16 bytes) — every character
   should be echoed back correctly, in order, with none dropped or
   reordered. This is the real behavioral difference from polling: bytes
   arriving while the CPU is doing something else (e.g. printing a
   response) are no longer lost, because the ISR captures them into the
   ring buffer as they arrive instead of requiring `uart_getc()` to be
   actively polling `RXC0` at the exact moment.
4. Regression-check every shell command still works, since they all go
   through this same RX path: `help`, `info`, `echo <text>`,
   `gpio <pin> on|off`, `uptime`.

**Status:** build-verified only (clean `make`, disassembly-verified vector
table — offset `0x48` jumps to the same `__vector_18` address as
`ISR(USART_RX_vect)` in `drivers/uart.c`). Hardware test pending (needs a
physical Uno) — required before this milestone counts as done per
`CLAUDE.md`.
