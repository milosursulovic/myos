# UART input / echo — Milestone 4

## What it does

`kernel/kernel.c` still prints the Milestone 3 banner and `myos>` prompt on
boot, but the trailing `for (;;) { }` (which previously did nothing) now
reads and echoes characters forever:

```c
for (;;) {
    char c = uart_getc();
    uart_putc(c);
}
```

`uart_getc()` (Milestone 2, `drivers/uart.c`) blocks on `RXC0` until a byte
arrives in `UDR0`, and `uart_putc()` blocks on `UDRE0` and writes it straight
back out. No new driver code was needed — Milestone 2's blocking
`uart_getc()`/`uart_putc()` API is exactly what this milestone requires.

This is **raw, single-character echo only**:

- No line buffering — nothing is held until Enter is pressed.
- No backspace/line-editing handling — a backspace or delete byte is
  echoed back as whatever raw byte it is (e.g. `0x08` or `0x7F`), not
  interpreted.
- No command parsing — typed text has no meaning yet, it's just bounced
  back to the terminal.

All of that (line buffer, backspace handling, `myos>` re-prompting per
line, command dispatch) is Milestone 5 (Shell) and is deliberately not
implemented here. This milestone only proves the receive path of the UART
driver works end-to-end: a character sent from the host makes it into
`UDR0` and back out again unmodified.

## How to test manually

1. `make flash PORT=<your port>` — flashes over Optiboot.
2. Connect a serial terminal to the board's USB port at **9600 8N1** (e.g.
   `screen /dev/ttyACM0 9600` or `picocom -b 9600 /dev/ttyACM0`).
3. Reset the board (or re-flash). Expect the Milestone 3 banner and
   `myos>` prompt once, as before.
4. Type characters. Each one should appear immediately, echoed back by the
   kernel (not by local terminal echo — if your terminal doesn't itself
   echo keystrokes, e.g. `screen`, you should still see each character
   appear on the exact keypress, one byte round-tripped through the
   ATmega328P at a time). Enter/backspace produce whatever raw control
   byte the terminal sends, echoed as-is — there is no line editing yet.

**Status:** build-verified only. Hardware test pending (needs a physical
Uno) — required before this milestone counts as done per `CLAUDE.md`.
