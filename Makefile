MCU     = atmega328p
F_CPU   = 16000000UL
PORT   ?= /dev/ttyACM0
BAUD   ?= 115200

CC      = avr-gcc
OBJCOPY = avr-objcopy
SIZE    = avr-size
AVRDUDE = avrdude

SRC_DIRS  = boot kernel drivers shell
C_SRCS    = $(wildcard $(addsuffix /*.c,$(SRC_DIRS)))
ASM_SRCS  = $(wildcard $(addsuffix /*.S,$(SRC_DIRS)))
OBJS      = $(addprefix build/,$(notdir $(C_SRCS:.c=.o))) \
            $(addprefix build/,$(notdir $(ASM_SRCS:.S=.o)))

vpath %.c $(SRC_DIRS)
vpath %.S $(SRC_DIRS)

CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall -Wextra -std=gnu99 \
          -ffreestanding -Iinclude -I.
ASFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Iinclude -I.
LDFLAGS = -mmcu=$(MCU) -nostartfiles -nodefaultlibs \
          -Wl,-T,linker.ld -Wl,--gc-sections -Wl,-Map,build/myos.map
# libgcc provides the compiler's software arithmetic helpers (e.g. integer
# division on AVR, which has no hardware divide instruction) — it's the
# compiler runtime, not avr-libc/libc, so it stays linked even with
# -nodefaultlibs. Must come after $(OBJS) so the linker resolves symbols
# the objects reference (library search is left-to-right).
LDLIBS  = -lgcc

.PHONY: all size flash clean bootloader myos-bin test-harness tools

all: build/myos.hex size

build/myos.elf: $(OBJS) linker.ld | build
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

build/myos.hex: build/myos.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/%.o: %.S | build
	$(CC) $(ASFLAGS) -c -o $@ $<

build:
	mkdir -p build

size: build/myos.elf
	$(SIZE) --format=avr --mcu=$(MCU) build/myos.elf

flash: build/myos.hex
	$(AVRDUDE) -c arduino -p $(MCU) -P $(PORT) -b $(BAUD) -D -U flash:w:build/myos.hex:i

clean:
	rm -f build/*.o build/*.elf build/*.hex build/*.map build/*.bin build/myos-upload

# ---------------------------------------------------------------------------
# Milestone 14 additions below. All additive: none of the targets/rules
# above this point are changed by any of this. See docs/bootloader.md.
# ---------------------------------------------------------------------------

# build/myos.bin: raw binary form of the (unmodified) kernel build, used by
# tools/myos-upload.c as its input image instead of the .hex avrdude/
# Optiboot use for `make flash`. Kept as its own target rather than folded
# into `all` so `make`'s existing output/behavior is untouched.
myos-bin: build/myos.bin

build/myos.bin: build/myos.elf
	$(OBJCOPY) -O binary $< $@

# `make bootloader`: builds the real MyOS bootloader (bootloader/boot_start.S
# + bootloader/boot_main.c) linked against bootloader/linker.ld, targeting
# the 0x7C00 Boot Loader Section. Building this does NOT install/flash it
# anywhere -- see docs/bootloader.md's "Deferred" section for why (needs ISP
# hardware not yet available) and the avr-size budget this must fit.
bootloader: build/bootloader.hex
	$(SIZE) --format=avr --mcu=$(MCU) build/bootloader.elf

build/bootloader.elf: build/boot_start.o build/boot_main.o bootloader/linker.ld | build
	$(CC) -mmcu=$(MCU) -nostartfiles -nodefaultlibs \
	    -Wl,-T,bootloader/linker.ld -Wl,--gc-sections -Wl,-Map,build/bootloader.map \
	    -o $@ build/boot_start.o build/boot_main.o -lgcc

build/bootloader.hex: build/bootloader.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

build/boot_start.o: bootloader/boot_start.S | build
	$(CC) $(ASFLAGS) -c -o $@ $<

build/boot_main.o: bootloader/boot_main.c bootloader/boot_main.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

# `make test-harness`: pre-ISP validation harness. Runs the EXACT SAME
# protocol/SPM logic (build/boot_main.o, above) as a normal application,
# entered via the existing kernel boot/start.S + linker.ld path so it's
# flashable via the existing, safe `make flash` (Optiboot) mechanism --
# NOT bootloader/linker.ld, and NOT installed into the Boot Loader Section.
# See docs/bootloader.md for how to run it and the scratch flash address
# used. build/start.o is the SAME object `all` builds (identical rule,
# reused rather than duplicated).
test-harness: build/test_harness.hex
	$(SIZE) --format=avr --mcu=$(MCU) build/test_harness.elf

build/test_harness.elf: build/start.o build/boot_main.o build/test_harness_main.o linker.ld | build
	$(CC) -mmcu=$(MCU) -nostartfiles -nodefaultlibs \
	    -Wl,-T,linker.ld -Wl,--gc-sections -Wl,-Map,build/test_harness.map \
	    -o $@ build/start.o build/boot_main.o build/test_harness_main.o $(LDLIBS)

build/test_harness.hex: build/test_harness.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

build/test_harness_main.o: bootloader/test_harness_main.c bootloader/boot_main.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

# `make tools`: build/myos-upload, the PC-side update tool
# (tools/myos-upload.c). Compiled with the system `cc`, NOT avr-gcc -- this
# runs on the developer's machine (opens a serial port, reads a file from
# disk), not on the ATmega328P, so none of the AVR cross-compilation flags
# above apply to it.
TOOLS_CC     = cc
TOOLS_CFLAGS = -Wall -Wextra -std=gnu99 -O2

tools: build/myos-upload

build/myos-upload: tools/myos-upload.c | build
	$(TOOLS_CC) $(TOOLS_CFLAGS) -o $@ $<
