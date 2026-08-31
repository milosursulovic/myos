# Shell — Milestone 5

## What it does

`shell/shell.c` replaces the Milestone 4 raw single-character echo loop
with a real line-editing shell. `kernel/kernel.c` is now just:

```c
void kernel_main(void)
{
    uart_init();
    shell_run();
}
```

`shell_run()` prints the banner (moved here from `kernel.c` — it's shell
startup output now, not a bare kernel print, and this way the banner text
lives in exactly one place), then loops forever: print the `myos>` prompt,
read a line, dispatch it, repeat.

## Line editing

Input is read one byte at a time via `uart_getc()` into a fixed 32-byte
buffer (`LINE_MAX` in `shell/shell.c`). 32 bytes comfortably fits a command
name plus a short argument (e.g. `echo hello`) without wasting SRAM on a
2KB-SRAM device — the buffer is `static`, so it costs 32 bytes of `.bss`
for the life of the kernel.

- **Backspace**: both `0x08` (BS) and `0x7F` (DEL) are treated as
  backspace, since different terminals send different codes. If the line
  is non-empty, the last buffered character is dropped and visually erased
  with `\b \b` (cursor back, overwrite with space, cursor back again). If
  the line is already empty, backspace is a no-op — it does not underflow
  or echo anything.
- **ENTER**: `\r` terminates the line. A bare `\n` is ignored so that a
  `\r\n` pair sent by a terminal doesn't trigger the line handler twice.
- **Printable input**: any other byte is echoed back with `uart_putc()`
  and appended to the buffer, unless the buffer is full (`LINE_MAX - 1`
  characters, leaving room for the NUL terminator), in which case the
  byte is silently dropped — no overflow, no echo.

## Command dispatch

Once ENTER is seen, the line is split in place at the first space into a
command token and an argument string (leading spaces before the argument
are skipped). The command token is compared against a small fixed table
using `str_eq_p()` (a local flash-vs-RAM string compare — `strcmp` isn't
used because the Makefile links with `-nodefaultlibs`, so avr-libc's
`string.h` functions aren't available).

Commands implemented in this milestone:

| Command | Behavior |
|---|---|
| `help`  | Lists the currently implemented commands (`help`, `info`, `echo`). |
| `info`  | Prints a static system info block (OS, version, CPU, clock, flash/RAM/EEPROM sizes — fixed device constants, not runtime-measured). |
| `echo`  | Prints back its argument string verbatim, followed by a newline. With no argument, prints a blank line. |

An unrecognized command prints `Unknown command: <name>` and reprompts —
it does not hang or crash the shell.

Future milestones add more commands (`uptime` needs Timer/Milestone 7,
`gpio` needs the GPIO driver/Milestone 6, `mem` needs the memory
manager/Milestone 10, `reboot` needs the watchdog/Milestone 33) — these
are deliberately **not** stubbed out yet. Adding a new command later means
adding a handler function and one more `str_eq_p(cmd, PSTR("name"))`
branch in `dispatch()` in `shell/shell.c`, plus listing it in `cmd_help()`.

All static shell strings (banner, prompt, command output labels) are
stored in flash via `PSTR`/`uart_puts_P`, matching the convention
established in Milestone 3. `uart_puts()` (plain RAM) is only used for
strings that aren't compile-time constants: the user's own argument buffer
(`echo`) and the unrecognized command name (`Unknown command: ...`).

## How to test manually

1. `make flash PORT=<your port>` — flashes over Optiboot.
2. Connect a serial terminal at **9600 8N1** (e.g.
   `screen /dev/ttyACM0 9600` or `picocom -b 9600 /dev/ttyACM0`).
3. Reset the board (or re-flash). Expect the banner followed by `myos>`.
4. Type `help` then ENTER — expect `help`, `info`, `echo` listed, then a
   fresh `myos>` prompt.
5. Type `info` then ENTER — expect the fixed system info block.
6. Type `echo hello` then ENTER — expect `hello` printed back.
7. Type a nonsense command, e.g. `foo`, then ENTER — expect
   `Unknown command: foo`.
8. Type a few characters, then press Backspace (or Delete, depending on
   your terminal) a few times — expect each backspace to erase one
   character on screen, and confirm the edited line (not the original) is
   what actually gets dispatched. Press Backspace with an empty line —
   expect no visible effect and no crash.

**Status:** build-verified only. Hardware test pending (needs a physical
Uno) — required before this milestone counts as done per `CLAUDE.md`.
