# GPIO — Milestone 6

## What it does

`drivers/gpio.c` / `drivers/gpio.h` implement a direct-register GPIO driver
for the 14 digital pins on the Arduino Uno (D0-D13), per spec section 19.
`shell/shell.c` gets a `gpio <pin> on|off` command that drives a pin high
or low through this driver.

## Pin mapping

The ATmega328P has three 8-bit I/O ports (B, C, D); the Uno's D0-D13
silkscreen pins map onto ports B and D as follows:

| Digital pin | Port | Bit |
|---|---|---|
| D0-D7   | PORTD | bit = pin |
| D8-D13  | PORTB | bit = pin - 8 |

(D13, notably, is PB5 — the Uno's built-in LED pin.) Pins 14 and above
(the analog-only A0-A5 pins, on PORTC) are out of scope for this milestone
and are treated as invalid pin numbers.

Each AVR I/O port has three registers: `DDRx` (data direction, 1 = output),
`PORTx` (output value when the pin is an output, or pull-up enable when
it's an input), and `PINx` (the actual input pin state — always reflects
real voltage on the pin, regardless of direction).

## API

```c
void gpio_init(void);
void gpio_set(unsigned char pin);
void gpio_clear(unsigned char pin);
unsigned char gpio_read(unsigned char pin);
```

- **`gpio_init()`** — explicitly clears the `DDRD`/`DDRB` bits for all 14
  managed pins so they start as inputs. This matches the ATmega328P's
  reset default already, but it's written explicitly rather than relied
  upon, so the driver's initial state doesn't depend on an unstated
  assumption about reset behavior. Called once from `kernel_main()`
  alongside `uart_init()`.
- **`gpio_set(pin)` / `gpio_clear(pin)`** — the spec's API has no separate
  "set direction" call, and driving a pin high or low only makes sense for
  an output, so both functions set that pin's `DDRx` bit to 1 (output)
  before writing the `PORTx` bit high or low. This means calling
  `gpio_set()`/`gpio_clear()` on a pin also makes it an output as a side
  effect — there's no way to drive a pin's level without doing so.
- **`gpio_read(pin)`** — reads the pin's `PINx` bit and returns 0 or 1.
  It does **not** change the pin's `DDRx` bit, since `PINx` reflects the
  real pin voltage regardless of direction (reading works the same whether
  the pin is currently an input or an output).
- **Invalid pins** (anything outside 0-13): the API has no error-reporting
  mechanism, so `gpio_set()`/`gpio_clear()` silently no-op (no register is
  written at all) and `gpio_read()` returns 0.

## Shell command

```
myos> gpio 13 on
GPIO 13: ON

myos> gpio 13 off
GPIO 13: OFF
```

`shell/shell.c`'s `cmd_gpio()` manually parses the argument string into a
decimal pin number (digit-by-digit — no `strtol`, `-nodefaultlibs` means
avr-libc's string/stdlib functions aren't linked) and an `on`/`off` mode
token (compared with the existing `str_eq_p()` PROGMEM-string helper). A
malformed command — non-numeric pin, pin outside 0-13, or a mode token
that isn't exactly `on`/`off` — prints `Usage: gpio <pin> on|off` instead
of guessing or silently doing nothing. The pin number in the output is
printed with the new `uart_put_uint()` helper (`drivers/uart.c`), a
minimal manual decimal-conversion routine (no `snprintf`/`itoa`).

There is no `gpio <pin> read` subcommand yet — the spec's milestone-6
shell example only covers `on`/`off`, so that's all that's wired up, even
though `gpio_read()` is implemented as part of the required driver API.

## How to test manually

1. `make flash PORT=<your port>`.
2. Connect a serial terminal at 9600 8N1.
3. Type `gpio 13 on` then ENTER — expect `GPIO 13: ON`, and the Uno's
   built-in LED (wired to D13/PB5) should light up.
4. Type `gpio 13 off` then ENTER — expect `GPIO 13: OFF`, and the LED
   should turn off.
5. Try a malformed command, e.g. `gpio 99 on`, `gpio 5 up`, or `gpio` with
   no arguments — expect `Usage: gpio <pin> on|off` in each case, with no
   crash or hang.
6. Type `help` — expect `gpio` listed alongside `help`, `info`, `echo`.

**Status:** build-verified only. Hardware test pending (needs a physical
Uno) — required before this milestone counts as done per `CLAUDE.md`.
