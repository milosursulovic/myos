# UART — Milestone 2

## What it does

`drivers/uart.c` / `drivers/uart.h` implement a polling driver for
ATmega328P USART0, configured for 9600 baud, 8 data bits, no parity, 1 stop
bit (8N1) — per spec section 14. No interrupts are used yet (RX interrupt +
ring buffer come in Milestone 9); transmit and receive both block on status
flags.

## Registers used

- **`UBRR0H` / `UBRR0L`** — baud rate divisor. Set once in `uart_init()`
  from `UBRR = F_CPU / (16 * BAUD) - 1` (datasheet USART "Examples of UBRRn
  Settings", asynchronous normal mode, `U2X0 = 0`). With `F_CPU = 16000000`
  and `BAUD = 9600` this evaluates to 103 at compile time.
- **`UCSR0B`** — `RXEN0` and `TXEN0` bits enable the receiver and
  transmitter. No interrupt-enable bits (`RXCIE0`/`TXCIE0`) are set — this
  milestone is polling-only.
- **`UCSR0C`** — `UCSZ01:UCSZ00 = 11` selects 8-bit character size. Parity
  bits left at their reset value (disabled = no parity), `USBS0` left at
  its reset value (1 stop bit) — matches 8N1.
- **`UCSR0A`** — read-only status bits polled by the blocking calls:
  - `UDRE0` (USART Data Register Empty) — `uart_putc()` spins until this is
    set before writing `UDR0`, so a byte isn't overwritten before the
    hardware has moved the previous one into the shift register.
  - `RXC0` (USART Receive Complete) — `uart_getc()` spins until this is set,
    meaning a full byte has arrived in `UDR0`.
- **`UDR0`** — the single I/O address that is both the transmit buffer
  (write) and receive buffer (read).

## API

```c
void uart_init(void);        // configure USART0 for 9600 8N1
void uart_putc(char c);      // block until UDRE0, then send one byte
void uart_puts(const char *str);   // send a NUL-terminated string from RAM
void uart_puts_P(const char *str); // send a NUL-terminated string from flash (PROGMEM)
char uart_getc(void);        // block until RXC0, then return one byte
```

### `uart_puts` vs `uart_puts_P`

AVR is a Harvard-architecture MCU: flash and RAM are separate address
spaces, and a plain C pointer read (`*str`) always compiles to a
data-space `ld` instruction. `uart_puts()` is correct for a buffer that
actually lives in RAM (e.g. a future shell input line). It is **not**
correct for a C string literal, because MyOS's `linker.ld` places
`.rodata`/`.progmem.data` directly in flash with no copy-to-RAM step at
boot — a string literal's "address" is really a flash byte offset, and
reading it with `ld` picks up whatever happens to be at that same address
in data space (I/O registers, on the ATmega328P, for small offsets).

`uart_puts_P()` is for strings that live in flash — build them with
`PSTR("...")` from `<avr/pgmspace.h>` — and reads each byte with
`pgm_read_byte()`, which compiles to `lpm`, the flash-space load
instruction. Use `uart_puts_P(PSTR("..."))` for any string literal;
use `uart_puts()` only for a genuine RAM buffer.

## How to test manually

This milestone only adds the driver — nothing in `kernel_main()` calls it
yet (that wiring, and the first visible UART output, is Milestone 3). So
there is no on-hardware behavior to observe yet.

1. `make` — builds cleanly. Since the driver isn't referenced from
   `kernel_main()`, the linker's `--gc-sections` strips it entirely, so
   flash/SRAM usage is unchanged from Milestone 1.
2. Once Milestone 3 wires `uart_init()` + `uart_puts()` into
   `kernel_main()`, the real test is: connect a serial terminal (e.g.
   `screen /dev/ttyACM0 9600` or similar) and confirm the expected text
   appears at 9600 8N1 after reset.

**Status:** hardware-verified (2026-08-31) — confirmed at 9600 8N1 over
`/dev/ttyUSB0` via every later milestone's shell session (banner, echo,
shell command output all round-trip correctly). Note RX is no longer
polling-based as of Milestone 9 — see `docs/uart-rx-interrupt.md`.
