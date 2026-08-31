# MyOS

Bare-metal operating system for the Arduino Uno R3 (ATmega328P), written from
scratch. Full spec: `docs/MyOS — Bare-Metal Operating System for Arduino Uno - Google Docs.pdf`.
Read it before making architectural decisions — this file only summarizes the
rules that must never be violated.

## Target hardware

- ATmega328P, AVR 8-bit, 16 MHz
- Flash: 32 KB, SRAM: 2 KB, EEPROM: 1 KB
- Board: Arduino Uno R3 (Optiboot bootloader present, USB-serial for upload)

## Hard rule: no Arduino framework

Never use the Arduino framework, Arduino Core, `setup()`, `loop()`,
`digitalWrite()`, `Serial`, `delay()`, or any Arduino library. Program the
ATmega328P directly:

- AVR registers (SFRs)
- AVR instructions
- Interrupt vectors
- Hardware peripherals (UART, Timer, GPIO, EEPROM, Watchdog, ...) via their
  datasheet-defined registers

`avr-libc` headers (`avr/io.h`, `avr/interrupt.h`, `avr/eeprom.h`,
`avr/wdt.h`) are allowed — they only define register names/addresses and
vector macros, they are not the Arduino framework. `avr-libc`'s runtime
startup (`crt0`, default vector table, `main()` wrapper) is NOT used — the
build excludes it with `-nostartfiles`. MyOS provides its own `_start` (see
`boot/start.S`).

The ATmega328P datasheet is the primary source of truth for how a peripheral
works. Do not derive register behavior from what the Arduino API happens to
do — read the datasheet section (referenced in spec section 43) instead.

## Development principle: strictly incremental

The project has 14 milestones (spec sections 13–28, full list in
`README.md`). Work one milestone at a time. Never implement multiple
milestones in one change. A milestone is only done when, in this order:

1. it compiles (`make`)
2. it flashes (`make flash`)
3. it is tested on a real Arduino Uno
4. it is documented (short note in `docs/`)

...only then move to the next milestone. If a milestone is unverified on
hardware, say so explicitly — don't claim it's done from a clean build alone.

Priority order when trade-offs come up: **Correctness > Understanding >
Testability > Performance > Optimization.** Do not optimize prematurely —
get it working and understandable first.

## Phase 1 constraint: don't touch the bootloader

Milestones 1–13 build on top of the existing Optiboot bootloader. Do not
modify fuse bits or replace the bootloader until the kernel itself is fully
functional (that's Milestone 14 — Custom Bootloader). `linker.ld` reserves
the top of flash for Optiboot; don't change that boundary before Milestone
14.

## Build & flash

```
make              # build build/myos.elf and build/myos.hex, print size
make flash        # flash via avrdude/Optiboot (PORT=/dev/ttyACM0 by default)
make flash PORT=/dev/ttyUSB0
make clean
```

Requires: `gcc-avr binutils-avr avr-libc avrdude make` (`sudo apt install
gcc-avr binutils-avr avr-libc avrdude make`).

## Project structure

```
myos/
├── boot/        _start (reset entry, stack init, jumps to kernel_main)
├── kernel/      kernel.c, memory manager, timer, scheduler
├── drivers/     uart.c/h, gpio.c/h, ...
├── shell/       command shell
├── include/     shared headers
├── bootloader/  custom bootloader (Milestone 14 only, untouched until then)
├── tools/       host-side tools (e.g. myos-upload, Milestone 31)
├── build/       build output (gitignored)
├── linker.ld
├── Makefile
└── docs/        spec + per-milestone notes
```

## Reference

Full milestone list, shell command spec, custom update protocol, and testing
strategy are in the `docs/` spec PDF and in `README.md`'s roadmap section —
check there before assuming a detail.
