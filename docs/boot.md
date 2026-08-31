# Boot — Milestone 1

## What it does

On reset, the ATmega328P begins executing at flash address `0x0000`
(Optiboot jumps here after its own timeout/boot window, per spec section 9).
`boot/start.S` provides `_start`, linked at `0x0000` via the `.vectors`
input section (see `linker.ld`), so it runs first — before any C code.

`_start`:

1. sets the stack pointer (`SPH:SPL`) to `RAMEND` (`0x08FF` on the
   ATmega328P — 2 KB SRAM starting at `0x0100`)
2. clears `SREG` (interrupts stay disabled — no interrupt-driven code exists
   yet)
3. calls `kernel_main()` (`kernel/kernel.c`)
4. if `kernel_main()` ever returns (it shouldn't — it's an infinite loop),
   falls into `hang:` — an infinite `rjmp` — rather than executing whatever
   garbage follows in flash

`kernel_main()` currently does nothing but loop forever. No output, no
peripherals initialized yet — that's Milestone 2+.

## API

None yet. `kernel_main(void)` is the only kernel entry point, called once
by `_start` and never expected to return.

## How to test manually

1. `make` — should build cleanly, `avr-size` should show a tiny flash
   footprint (currently 18 bytes) and 0 bytes of SRAM data/bss.
2. `make flash PORT=<your port>` — flashes over Optiboot.
3. Hardware check: the board should sit stably running (no visible
   behavior is expected yet — there's no LED or UART output in this
   milestone). A basic sanity check is that the board doesn't reset in a
   loop (e.g. watch the TX/RX LEDs briefly flash once on upload/reset and
   then go idle, not blink repeatedly) — a genuine boot loop would indicate
   `_start` isn't landing at address 0 or the reset vector setup is wrong.

**Status:** hardware-verified (2026-08-31). Confirmed indirectly but
conclusively — the board boots reliably and every later milestone's shell
session over `/dev/ttyUSB0` at 9600 8N1 works, which is only possible if
`_start` lands at address 0 and `kernel_main()` is reached correctly.
