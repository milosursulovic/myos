---
name: myos-build-flash
description: Build and flash MyOS to a real Arduino Uno. Use when the user says "build", "compile", "flash", "upload", "test on the board", or asks to run/check the current MyOS milestone on hardware.
---

# MyOS build & flash

1. Run `make` from the project root. If it fails, report the exact
   compiler/linker error — don't guess at a fix without reading it.
2. On success, report the `avr-size` output (flash/SRAM usage) — flag it if
   it looks unexpectedly large for what was just added.
3. Ask the user which serial port the Uno is on if not already known
   (common defaults: `/dev/ttyACM0`, `/dev/ttyUSB0`). Do not guess and flash
   blind if a wrong port would be ambiguous — flashing is a hardware-facing,
   real-world action.
4. Run `make flash PORT=<port>`. This uses `avrdude` in `arduino` (Optiboot)
   programmer mode at 115200 baud — normal for Phase 1 (spec section 9: no
   custom bootloader yet).
5. If `avrdude` reports a sync/timeout error, the usual causes are: wrong
   port, board not in bootloader window (double-tap reset not needed on
   Uno — Optiboot auto-resets on DTR), or something else already has the
   port open (e.g. a serial monitor) — mention these rather than retrying
   blindly.
6. After a successful flash, remind the user this only proves the flash
   write succeeded — actually confirming the milestone works still needs
   them to observe the board (LED, UART terminal at the milestone's baud
   rate, etc.) per the milestone's Definition of Done.
