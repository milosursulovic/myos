# MyOS

MyOS is an experimental, minimal bare-metal operating system for the
**Arduino Uno R3** (ATmega328P), written from scratch — no Arduino
framework, no Arduino Core, no existing RTOS.

Full spec: [`docs/MyOS — Bare-Metal Operating System for Arduino Uno - Google Docs.pdf`](docs).
Claude Code project rules: [`CLAUDE.md`](CLAUDE.md).

The project is a practical introduction to:

- bare-metal development
- AVR architecture
- boot process, startup code, linker scripts
- UART communication, interrupts, timers, GPIO
- memory management
- a cooperative and later preemptive scheduler
- bootloader development
- a custom host↔device update protocol

## Target hardware

| | |
|---|---|
| Board | Arduino Uno R3 |
| MCU | ATmega328P |
| Architecture | AVR 8-bit |
| Clock | 16 MHz |
| Flash | 32 KB |
| SRAM | 2 KB |
| EEPROM | 1 KB |

## Principles

- **No Arduino framework.** No `setup()`/`loop()`, no `digitalWrite()`, no
  `Serial`, no `delay()`. Direct AVR registers, instructions, interrupt
  vectors, hardware peripherals instead. See [`CLAUDE.md`](CLAUDE.md) for
  the exact rule and what's allowed (`avr-libc` register headers).
- **Strictly incremental.** One milestone at a time. Each must compile,
  flash, be tested on real hardware, and be documented before the next
  starts. Priority order: **Correctness > Understanding > Testability >
  Performance > Optimization.**
- **Phase 1 keeps the existing Optiboot bootloader.** No fuse changes, no
  custom bootloader, until the kernel itself is fully functional
  (Milestone 14).

## Development environment

Linux, with:

```
sudo apt install gcc-avr binutils-avr avr-libc avrdude make
```

Verify:

```
avr-gcc --version
avr-objcopy --version
avrdude --version
```

## Build & flash

```
make                    # build build/myos.elf + build/myos.hex, print size
make flash               # flash via avrdude/Optiboot, PORT=/dev/ttyACM0 by default
make flash PORT=/dev/ttyUSB0
make clean
```

## Project structure

```
myos/
├── boot/         boot/start.S — reset entry, stack init, jump to kernel_main
├── kernel/       kernel.c, memory manager, timer, scheduler
├── drivers/      uart.c/h, gpio.c/h, ...
├── shell/        command shell
├── include/      shared headers
├── bootloader/   custom bootloader (Milestone 14 only)
├── tools/        host-side tools (e.g. myos-upload)
├── build/        build output (gitignored)
├── linker.ld
├── Makefile
└── docs/         spec + per-milestone notes
```

## Milestone roadmap

- [ ] **1. Kernel boot** — boots via Optiboot into `kernel_main()`, hangs in
      an infinite loop (build-verified; hardware test pending)
- [ ] 2. UART — `uart_init/putc/puts/getc`, 9600 8N1
- [ ] 3. First kernel output — prints the MyOS banner on boot
- [ ] 4. UART input — echoes received characters
- [ ] 5. Shell — command parser (`help`, `info`, `uptime`, `mem`, `gpio`,
      `echo`, `reboot`)
- [ ] 6. GPIO — `gpio_init/set/clear/read`
- [ ] 7. Timer — hardware timer, `system_ticks`, `timer_get_ticks()`,
      `uptime` shell command
- [ ] 8. Interrupt system — vector table, ISRs, global interrupt enable
- [ ] 9. UART interrupts — RX interrupt + ring buffer, replaces polling
- [ ] 10. Memory manager — `kmalloc`/`kfree`, `mem` shell command
- [ ] 11. Tasks — `task_t`, a few initial tasks (shell, LED, system service)
- [ ] 12. Scheduler (cooperative) — `scheduler_init/run`, `task_yield()`
- [ ] 13. Scheduler (preemptive) — timer-interrupt-driven context switch
- [ ] 14. Custom bootloader — drop Optiboot dependency, MyOS's own
      bootloader + Flash self-programming + custom update protocol +
      `tools/myos-upload`

Definition of Done for the whole project (spec section 44): self-hosted
kernel boots without Arduino framework dependence anywhere, shell works,
GPIO/timer/interrupts/memory/tasks/scheduler all work, custom bootloader
uploads a kernel over the custom protocol, EEPROM config and watchdog
recovery work, docs exist, and the whole thing builds with one `make`.

## Testing strategy

Every component needs a test, ideally on real hardware over UART. See spec
section 38 for the full list (boot, UART, GPIO, timer, interrupt, memory,
scheduler, bootloader tests). Debug output goes over UART during
development and is compiled out (`DEBUG=0`) in a final build.

## Documentation

Per-component notes live in `docs/` (e.g. `docs/uart.md`,
`docs/scheduler.md`) — what the hardware does, which registers are used,
how the code uses the hardware, and why a particular implementation choice
was made. See spec section 40.
