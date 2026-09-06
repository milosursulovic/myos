# Custom bootloader — Milestone 14 (build + pre-validate; on-device install deferred)

## Status

**Partial.** Code is written, builds clean, and fits its flash budget.
The protocol logic (handshake, packet parsing, CRC-8, ACK flow, and
critically the "never boot a bad image" safety mechanism) has been
hardware-tested end-to-end and works correctly. **The flash
self-programming (SPM) path itself has NOT been validated on hardware,
and — a real finding from this session, not just an untried step — it
cannot be, without ISP hardware.** See "Pre-ISP validation harness" below
for what actually happened when this was tried, and why. The on-device
install into the Boot Loader Section is **explicitly deferred** — see
"Deferred" at the bottom. `README.md`'s Milestone 14 checkbox stays
unchecked until that install and its hardware regression pass actually
happen.

## Why the install is deferred

Optiboot's serial protocol (what `make flash` has used for every earlier
milestone) can only write the *application* section — no serial bootloader
can overwrite its own code section while running from it. Installing a
*replacement* bootloader means writing directly into the Boot Loader
Section (BLS) itself, which requires ISP hardware (USBasp / AVRISP / a
second Arduino running ArduinoISP) — not currently available. A bad ISP
write with no ISP-based recovery path would brick the board with no
USB-serial way back. Per that constraint, this milestone covers everything
that can be done safely first: writing and building the bootloader and
host tool, and pre-validating the protocol/flash-write logic through the
existing, safe Optiboot/application-section path.

## Fuse / Boot Loader Section research

- `SPM_PAGESIZE` = 128 **bytes** (64 words) — confirmed directly against
  `/usr/lib/avr/include/avr/iom328p.h` and avr-libc's own `<avr/boot.h>`
  API-usage example, which loops `for (i = 0; i < SPM_PAGESIZE; i += 2)`
  filling one word per iteration. **This corrects this milestone's own
  planning notes**, which assumed 128 *words* = 256 bytes/page — that was
  wrong; the ATmega328P's real flash page is 128 bytes. `boot_main.c` uses
  the `SPM_PAGESIZE` macro everywhere instead of a literal, specifically so
  it can't silently disagree with the toolchain again;
  `tools/myos-upload.c` hardcodes `128` (no avr-libc headers on the host
  side) with a comment pointing back here.
- `FLASHEND` = `0x7FFF` (32KB, addresses `0x0000`-`0x7FFF`).
- ATmega328P Boot Loader Section sizing (`BOOTSZ1:0` fuse bits, in
  `hfuse`), per the Microchip datasheet's Boot Loader Parameters table:
  `11`→512B@`0x7E00`, `10`→1KB@`0x7C00`, `01`→2KB@`0x7800`, `00`→4KB@`0x7000`.
- Standard Arduino Uno factory fuses: `lfuse=0xFF`, `hfuse=0xDE`,
  `efuse=0xFD` (independently confirmed via Arduino's own `boards.txt`,
  which sets `upload.maximum_size=32256 = 32768-512`, i.e. Optiboot
  occupies exactly 512 bytes). Decoded: `BOOTRST=0` (already vectors into
  the boot section — how Optiboot is entered today), `BOOTSZ1:0=11`
  (512B@`0x7E00`).
- `linker.ld`'s own comment previously claimed Optiboot needs 1KB — fixed;
  it needs 512B. `linker.ld`'s `FLASH LENGTH = 0x7C00` was always
  deliberately larger than that (reserving room for *this* milestone's own
  bigger boot section), so nothing there ever actually overlapped.
- **Decision: MyOS's own bootloader targets a 1KB boot section
  (`BOOTSZ1:0=10`)**, not the stock 512B. A from-scratch protocol
  (framing + CRC + SPM page writes over polling UART) is unlikely to fit
  512 bytes without brutal, error-prone size-golfing — Optiboot's tight
  footprint is the product of years of hand-optimization with no reason to
  reproduce here (Correctness > Optimization). This needs **zero change**
  to the existing `linker.ld` (`FLASH LENGTH = 0x7C00` already reserves
  exactly this). The actual build fits comfortably inside that budget —
  see "Build results" below.
- **Mandatory before ever touching real fuses**: read the actual current
  fuse bytes via ISP first (`avrdude -c usbasp -p atmega328p -U
  lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h`) and confirm they match the
  assumed standard values above — never write fuses from assumption alone.
  (An earlier attempt to read fuses *through Optiboot* returned `0x0` for
  all three — not trustworthy; Optiboot's serial protocol doesn't
  genuinely expose ISP-level fuse reads.)

## Bootloader design (`bootloader/`)

- **Polling only, no interrupts, no vector table.** The AVR's interrupt
  vector table lives at a *fixed* flash address (`0x0000`-`0x0067`ish)
  regardless of `BOOTRST` — physically part of the *kernel* image this
  bootloader might be mid-overwrite of. Relying on vectors that could be
  half-rewritten (or belong to whatever kernel used to be there) is
  unsafe; polling needs none of them. When `BOOTRST` is eventually
  programmed (deferred), the hardware reset vector becomes the start of
  the Boot Loader Section itself — execution begins directly there, no
  jump table needed at all.
- `bootloader/boot_start.S` — stack pointer init only (same technique as
  `boot/start.S`'s `_start`), no `.data`/`.bss` copy-down. `boot_main.c` is
  written to keep all state in locals/registers specifically so that
  machinery isn't needed, keeping this build as small as possible.
- `bootloader/boot_main.c` — the protocol state machine, own minimal
  polling UART (9600 8N1, matching the kernel's own baud — NOT a reuse of
  `drivers/uart.c`'s interrupt-driven ring buffer, which needs `RXCIE0`/an
  ISR/the vector table, all ruled out above), and flash self-programming
  via `<avr/boot.h>`'s `boot_page_erase()`/`boot_page_fill()`/
  `boot_page_write()`/`boot_spm_busy_wait()`/`boot_rww_enable()` macros —
  not hand-rolled SPM assembly. These are compile-time macros implementing
  the datasheet-mandated timed SPM sequence (same category as
  `<avr/interrupt.h>`'s `ISR()` or `<avr/pgmspace.h>`'s `pgm_read_byte()`,
  already used throughout this project), far lower-risk than reimplementing
  that timing by hand given the stakes (a bad self-write here has no
  ISP-free recovery once this code actually lives in the BLS).
- `bootloader/linker.ld` — new, small script: `FLASH ORIGIN=0x7C00
  LENGTH=0x400`, `ENTRY(_start)`. Building this doesn't install or flash
  it anywhere — see "Deferred" below.

### Flow (spec section 29)

```
RESET -> UART init -> wait for HELLO (bounded, ~1-2s)
  timeout          -> jump to kernel at 0x0000
  HELLO received   -> READY
                    -> receive KERNEL_INFO -> OK
                    -> receive PAGE (repeat) -> ACK (repeat)
                    -> receive VERIFY
                         CRC matches    -> OK -> receive BOOT -> jump to kernel
                         CRC mismatch   -> ERROR -> back to "wait for HELLO"
                                                     (never boots a bad image)
```

The bounded wait-for-HELLO timeout is a plain busy-loop cycle counter
(`BOOT_HELLO_TIMEOUT_LOOPS` in `boot_main.c`), not a hardware timer — kept
interrupt-free/timer-free to match the rest of this file. Its duration is
therefore approximate and proportional to `F_CPU`, not a calibrated
wall-clock value (roughly 1-2 seconds at 16MHz) — an accepted
simplification for a one-shot "is anyone there" window, not something
needing millisecond precision.

## Update protocol (spec sections 30-31)

Packet format:

```
MAGIC(2) | TYPE(1) | LENGTH(2, little-endian) | SEQUENCE(1) | PAYLOAD(<=256) | CRC(1)
```

`MAGIC` = `0xA5 0x5A`. CRC-8, polynomial `0x07` (CRC-8-CCITT/CRC-8-ATM), no
reflection, initial value `0`, computed over `TYPE`/`LENGTH`/`SEQUENCE`/
`PAYLOAD` only (not `MAGIC`, not itself) — chosen over CRC-16 for
simpler/smaller code on the 1KB-constrained device side, adequate for
catching transmission errors on packets this small. Implemented
identically in `bootloader/boot_main.c` (`crc8_update()`) and
`tools/myos-upload.c` (`crc8_update()`).

For `PAGE` packets specifically, the header's existing `SEQUENCE` byte
doubles as the (0-based) page number, and `PAYLOAD` is exactly
`SPM_PAGESIZE` (128) bytes of that page's raw data — no separate page
number needs encoding inside the payload.

```
HOST -> HELLO                         DEVICE -> READY
HOST -> KERNEL_INFO(base_addr,        DEVICE -> OK
         total_size, expected_crc)
HOST -> PAGE(page#, 128B)             DEVICE -> ACK        (repeat per page)
HOST -> VERIFY                        DEVICE -> OK | ERROR
HOST -> BOOT                          (device jumps to the image at base_addr)
```

`KERNEL_INFO`'s payload is `base_addr(2, LE) | total_size(4, LE) |
expected_crc(1)` = 7 bytes. **`base_addr` is an addition beyond the
literal spec text** (which only mentions "total size") — it's what makes
`boot_main.c`'s protocol logic genuinely address-agnostic: it never
hardcodes `0x0000` (the real deployment target — the kernel image the
real bootloader would overwrite), `0x7C00` (its own BLS), or the test
harness's scratch address anywhere; the host tells it where to write on
every run. Without this field, the harness described below would have had
no safe way to redirect writes away from its own running code. `VERIFY`'s
CRC-8 is computed by the device by reading back the *actual* just-written
flash (via `pgm_read_byte()`), not the copy that was sent — a genuine SPM
failure would show up here.

## Host tool (`tools/myos-upload.c`)

Plain C, compiled with the system `cc` (not `avr-gcc` — PC-side tooling,
not AVR target code). POSIX termios serial I/O, 9600 8N1. Consumes a raw
binary image (`build/myos.bin`, via the new `make myos-bin` target) rather
than parsing Intel HEX.

```
myos-upload <serial-port> <kernel-image.bin> [base-address-hex]
```

`base-address-hex` is optional and defaults to `0x0000` (the real
deployment target). It exists specifically to let this same tool drive the
pre-ISP validation harness below at a safe scratch address instead.

Flow: open+configure the port -> send `HELLO`, retrying a few times with a
short per-attempt timeout (opening a USB-serial port to an Uno commonly
toggles DTR and resets the MCU right as the port opens, so the first
attempt or two can race the bootloader's own wait window) -> `KERNEL_INFO`
(computed CRC-8 over the whole image, plus the target base address) -> wait
`OK` -> split the image into 128-byte pages (the last one padded with
`0xFF`, matching AVR's erased-flash value) and send each with its page
number, waiting `ACK` per page, printing `Page N/M ACKed` -> `VERIFY`, wait
`OK`/`ERROR` (reports clearly and exits nonzero on `ERROR` or a timeout,
without sending `BOOT`) -> `BOOT`.

## Pre-ISP validation harness (`bootloader/test_harness_main.c`)

Since the real bootloader can't be installed without ISP hardware, this
harness exercises the *exact same* protocol + SPM logic
(`bootloader/boot_main.c`, byte-for-byte unmodified, `#include`d nowhere —
compiled once and linked into both binaries) safely, via the already-proven
Optiboot/application-section path:

- Entry point is the existing `boot/start.S` + top-level `linker.ld` +
  `make flash` mechanics — the exact same, already hardware-verified path
  every earlier milestone's kernel build has used. `test_harness_main.c`
  only supplies `kernel_main()` (calls `boot_main()`) and two empty
  `ISR(TIMER0_COMPA_vect)`/`ISR(USART_RX_vect)` stubs, needed purely
  because `boot/start.S`'s vector table unconditionally `jmp`s to both
  symbols regardless of which binary is linked — neither ever actually
  fires, since `boot_main()` never calls `sei()` or enables `RXCIE0`/
  `OCIE0A`.
- `make test-harness` builds `build/test_harness.elf/.hex`, flashable
  exactly like the normal kernel: `avrdude -c arduino -p atmega328p -P
  <port> -b 115200 -D -U flash:w:build/test_harness.hex:i` (or copy it to
  `build/myos.hex` and use the existing `make flash` target verbatim).
- **Chosen scratch address: `0x2000`** (8192, a multiple of
  `SPM_PAGESIZE`). Confidence this doesn't collide with anything: the
  current kernel build is 3858 bytes (`build/myos.elf`'s `.text`, ends at
  `0xF12`) and the harness itself is 1110 bytes (ends at `0x456`) — `0x2000`
  leaves roughly 2x headroom past the larger of the two, and roughly 23.5KB
  of clear space before the real boot section at `0x7C00`. Both figures
  were read directly from `avr-size`/`avr-objdump` output on the actual
  built ELFs, not assumed.
- **What actually happened when this was run (2026-09-06, real Arduino
  Uno, `/dev/ttyUSB0`):**
  1. `make test-harness`, flashed via the normal safe `make flash`
     mechanics.
  2. `./build/myos-upload /dev/ttyUSB0 build/myos.bin 2000` — handshake
     succeeded, all **31/31 pages ACKed**, then **`VERIFY` came back
     `ERROR`** (CRC mismatch) — the host tool correctly refused to send
     `BOOT` and printed the "device is safe, waiting indefinitely" message
     (see "Review-driven safety fixes" above — this is the fix from bug
     #1 working exactly as designed, confirmed for real, not just by
     inspection).
  3. Independently read back flash at `0x2000` via `avrdude -U
     flash:r:-:i` (application-section read, no BLS access needed) and
     compared by hand against the sent image: **the bytes at `0x2000` were
     neither the sent data nor `0xFF` (erased)** — they were pre-existing
     flash content, completely unaffected by the 31 `write_page()` calls
     that had just each individually returned/ACKed normally.
  4. This is not a bug in this session's code — it's a real, ATmega328P
     hardware/datasheet restriction, confirmed by checking (not assuming):
     **the SPM instruction that writes flash must itself be executing
     from within the Boot Loader Section** (as currently defined by the
     BOOTSZ fuses) to actually take effect; SPM executed from the
     Application section (where this test harness necessarily runs,
     flashed via Optiboot like every other milestone's kernel) is
     restricted by hardware and has no effect on flash contents, without
     crashing or hanging — `boot_page_erase`/`fill`/`write`/
     `boot_spm_busy_wait()` all returned normally every time, which is
     exactly why the protocol layer on top of them (ACKs, timing) looked
     completely fine while the underlying writes silently did nothing.
  5. **Consequence for this milestone's own pre-ISP validation plan**:
     the SPM/flash-write path cannot be validated pre-ISP at all — not
     "wasn't validated yet," but structurally can't be, on this hardware,
     by any method available before real BLS code exists. The *only* way
     to exercise `write_page()` for real is to actually run it from the
     BLS, which requires the exact ISP install this whole milestone is
     deferring. This was not anticipated when the pre-ISP validation
     strategy was planned; it's a genuine finding from actually trying it,
     not something caught by review beforehand.

**What this pre-ISP pass DID genuinely validate** (real, on hardware, not
just by code review): the full packet protocol both directions (`HELLO`/
`READY`/`KERNEL_INFO`/`OK`/`PAGE`/`ACK`/`VERIFY`), CRC-8 computed
identically on host and device across a real 3858-byte/31-page transfer,
page-splitting/padding, and — the most safety-relevant part of the whole
milestone — that a failed `VERIFY` genuinely, observably leaves the device
refusing to boot and waiting indefinitely rather than falling through to
`jump_to_kernel()`. **What remains completely unvalidated**: the SPM
erase/fill/write sequence's actual effect on flash contents, `RWW`/`NRWW`
behavior, and `boot_rww_enable()` — all of it needs the real BLS install
to test at all, not just to test "for real" vs. "in a safe sandbox."

## Build results (this session)

- `make bootloader`: clean build, no warnings.
  `avr-size --format=avr build/bootloader.elf`:
  **1002 bytes** `.text` (+0 bytes `.data`/`.bss`) — fits the 1024-byte
  Boot Loader Section budget with **22 bytes to spare** (down from an
  initial 958/66-bytes-spare after the review-driven safety fixes below;
  the margin is genuinely tight now — a future change here should re-check
  `avr-size` immediately, not assume there's room).
  `avr-objdump -h` confirms no `.data`/`.bss` sections were actually
  emitted (checked, not assumed, per this file's own design goal of
  avoiding initialized/zero-requiring statics).
- `make tools`: clean build (`cc -Wall -Wextra -std=gnu99`), no warnings.
- `make test-harness`: clean build, no warnings. `.text` = 1110 bytes,
  0 bytes `.data`/`.bss`.
- `make` (existing kernel build): unaffected. `build/myos.elf`/
  `build/myos.hex` are byte-for-byte identical (verified via `md5sum`)
  before and after all of this milestone's changes.
- `make myos-bin`: produces `build/myos.bin` from the unmodified
  `build/myos.elf` via `avr-objcopy -O binary`.

## Review-driven safety fixes (before any hardware testing)

Code review (before this had ever touched real hardware) found and fixed
two real bugs in `boot_main()`, plus one bug found in the *fix* for the
first one — worth recording precisely, given how easy it is for a
plausible-looking safety fix to itself be wrong:

1. **Critical: the "never boot a bad image" guarantee was violated.** Any
   failure *after* `KERNEL_INFO` was accepted (a bad `PAGE`, a failed
   `VERIFY`) looped back to the same *bounded-timeout* `wait_for_hello()`
   used for the very first "is anyone there" wait — so if the host didn't
   retry within ~1-2s, the device would time out and boot the very memory
   it had just found corrupt or left partially written. Fixed: added
   `wait_for_hello_no_boot()` (loops `wait_for_hello()` forever, never
   returning on timeout) and routed every post-`KERNEL_INFO` resync
   through it. `jump_to_kernel()` is now reachable from exactly two
   places: the single pre-session wait (nothing touched yet), or an
   explicit, fully-verified `BOOT` command.
2. **High: no page-alignment or flash-bounds validation of `base_addr`.**
   An unaligned address makes `boot_page_fill()` calls straddle two
   physical hardware pages (the write only commits the page containing
   the address, silently losing the rest); an out-of-range address could
   have SPM alias back into low flash (the live vector table/kernel
   image) instead of failing. Fixed with an explicit check in both
   `boot_main()` and `tools/myos-upload.c` before any flash-modifying
   call.
3. **The fix for #2 had its own bug**: `total_size > (uint32_t)(0x8000UL
   - base_addr)` underflows in unsigned arithmetic when `base_addr >
   0x8000` (e.g. `0x8000 - 0xFF80` wraps to `0xFFFF8080`), making the
   check silently pass for exactly the out-of-range addresses it was
   meant to catch — fully defeating fix #2. Caught by a second review
   pass, not the first. Fixed by rejecting `base_addr >= 0x8000` outright
   *before* computing the subtraction, so it's only ever evaluated on a
   value already known to be safely below `0x8000`.

**Known, accepted limitation (not fixed):** `wait_for_hello()`'s read of
the second magic byte (and the rest of the packet, via
`recv_packet_body()`) is unbounded — a single stray/noise byte on the
line that happens to equal `MAGIC0`, with no real `MAGIC1` ever following,
would wedge the pre-session wait indefinitely instead of falling through
to `jump_to_kernel()`. A bounded fix was written and worked, but pushed
`build/bootloader.elf` to 1026 bytes, over the 1024-byte budget — reverted
in favor of staying within the hard size limit, given how narrow this
specific noise scenario is in practice (an exact single-byte coincidence
with zero follow-up). Worth revisiting if the boot section size is ever
increased.

## How to test manually (once ISP hardware is available)

See "Deferred" below for the exact install steps — after those, the full
regression list is:

1. Confirm the board still boots the existing kernel exactly as before
   (Milestones 1-13's full manual test list).
2. `make tools`, `make myos-bin`, then `./build/myos-upload <port>
   build/myos.bin` (default `base-address-hex` = `0x0000`) against the
   *real*, now-BLS-resident bootloader — confirm the same `Page N/M
   ACKed` / `Verify OK` / `Sent BOOT` flow works end-to-end for an actual
   kernel update, not just the harness's scratch-region validation.
3. Power-cycle with no host attached — confirm the bounded HELLO wait
   times out and the existing kernel boots normally (the "no update
   available" path).

## Deferred — requires ISP hardware

Not performed in this session; no `avrdude` command touching `hfuse`,
`efuse`, `lfuse`, or a BLS-address (`0x7C00`-`0x7FFF`) flash write was run.
Exact remaining steps, in order, when ISP hardware (USBasp/AVRISP/
ArduinoISP) is available:

1. **Read and confirm current fuses** — never write based on assumption
   alone, even a well-researched one:
   ```
   avrdude -c usbasp -p atmega328p -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h
   ```
   Confirm the read-back values match the "standard Arduino Uno factory
   fuses" above before proceeding.
2. **Write the bootloader into the Boot Loader Section**:
   ```
   avrdude -c usbasp -p atmega328p -U flash:w:build/bootloader.hex:i
   ```
   (The exact avrdude invocation for an address-scoped BLS write needs to
   be confirmed against avrdude's actual behavior at that time — the hex
   file's own address records should place it correctly at `0x7C00`, but
   this must be verified, not assumed, before relying on it.)
3. **Set `hfuse` for a 1KB boot section**: `BOOTSZ1:0` needs to change
   from `11` to `10` (bit 2 cleared): `0xDE & ~0x04 = 0xDA`.
   ```
   avrdude -c usbasp -p atmega328p -U hfuse:w:0xDA:m
   ```
   **Re-derive and re-verify this arithmetic at that time** against
   whatever step 1's real read-back values turn out to be — do not trust
   this document's arithmetic blindly months later.
4. **First-ever real SPM test, before trusting it with the real kernel
   region**: per "Pre-ISP validation harness" above, the erase/fill/write
   sequence has NEVER actually been exercised on this hardware — the
   pre-ISP attempt proved SPM from the Application section has no effect
   at all, so once code is genuinely running from the BLS, its very first
   real flash write is happening for the first time, period. Before
   trusting it with `0x0000` (the live kernel), repeat the exact
   `myos-upload ... build/myos.bin 0x2000` scratch-address test from this
   session — same expectation (31/31 pages ACKed, `VERIFY OK` this time),
   then independently read back `0x2000` via ISP (not Optiboot, which
   still won't need to touch anything sensitive for a plain read) and
   diff against `build/myos.bin` byte-for-byte. Only once that genuinely
   passes should step 5 (writing to `0x0000`, the address that matters)
   be attempted.
5. **Full regression**: all of Milestones 1-13's manual test steps on the
   now-custom-bootloader board, plus the "how to test manually" section
   above (a real kernel update via `myos-upload` targeting `0x0000`, not
   Optiboot, and the no-host-attached timeout-boots-existing-kernel path).

Only after all five steps pass should `README.md`'s Milestone 14 checkbox
be marked done.
