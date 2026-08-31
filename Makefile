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

.PHONY: all size flash clean

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
	rm -f build/*.o build/*.elf build/*.hex build/*.map
