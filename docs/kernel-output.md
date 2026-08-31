# Kernel output — Milestone 3

## What it does

`kernel/kernel.c` now calls `uart_init()` and then `uart_puts_P()`
(declared in `drivers/uart.h`, implemented by Milestone 2) to print a fixed
banner over USART0 immediately after boot, before falling into the
permanent `for (;;)` loop (per spec section 15).

Every banner literal is wrapped in `PSTR(...)` (from `<avr/pgmspace.h>`)
and sent with `uart_puts_P()`, not `uart_puts()`. `linker.ld` places string
literals straight into flash with no boot-time copy into SRAM, so a plain
`uart_puts()` call on a string literal would read flash-space bytes with a
data-space `ld` instruction — wrong address space entirely (see
`docs/uart.md`). Copying every literal into the 2KB SRAM at boot just to
use a plain pointer would also waste RAM for no benefit; `PSTR`/
`uart_puts_P` keep the banner text flash-resident and read it correctly
with `lpm`.

`kernel_main()` prints exactly:

```
====================================
        MyOS v0.1
====================================

ATmega328P kernel started.

myos>
```

`\r\n` is used for line breaks rather than a bare `\n` — most serial
terminals need the carriage return to actually return the cursor to column
0, since MyOS is talking directly to the wire with no OS-level newline
translation on either end. The final `myos>` has no trailing newline: it is
a prompt, not a completed line. There is no command parsing or input
handling yet — reading characters back is Milestone 4 (echo) and Milestone
5 (shell); this milestone is a static, one-shot print.

Because `kernel_main()` now actually calls `uart_init()`/`uart_puts()`,
the linker's `--gc-sections` can no longer strip `drivers/uart.c` out of
the final image, so flash usage is nonzero for the first time (see below).

## Why `kernel_main` depends on `uart.h`

`kernel/kernel.c` includes `drivers/uart.h` to get the prototypes for
`uart_init()`, `uart_puts()`, and `uart_puts_P()`. The Makefile's `CFLAGS`/
`ASFLAGS` include `-I.` (project root), so this include path is
root-relative rather than a fragile `../drivers/uart.h` relative to
`kernel/kernel.c`'s own directory.

## How to test manually

1. `make` — builds cleanly with no warnings. `avr-size` should now show a
   small but nonzero `.text` size (the banner strings plus the UART driver
   code), unlike Milestone 1/2 where the driver was unreferenced and
   stripped.
2. `make flash PORT=<your port>` — flashes over Optiboot.
3. Connect a serial terminal to the board's USB port at **9600 8N1**
   (e.g. `screen /dev/ttyACM0 9600` or `picocom -b 9600 /dev/ttyACM0`).
4. Reset the board (or re-flash, which resets it). Expect exactly:

   ```
   ====================================
           MyOS v0.1
   ====================================

   ATmega328P kernel started.

   myos>
   ```

   printed once, with no further output afterward (the kernel loops
   forever with nothing left to do).

**Status:** build-verified only. Hardware test pending (needs a physical
Uno) — required before this milestone counts as done per `CLAUDE.md`.
