#ifndef MYOS_BOOT_MAIN_H
#define MYOS_BOOT_MAIN_H

/* boot_main() implements the whole custom update protocol state machine
 * (see docs/bootloader.md). It is shared, unmodified, by two different
 * binaries:
 *
 *  - the real bootloader (bootloader/boot_start.S -> boot_main(), linked
 *    with bootloader/linker.ld against the 0x7C00 Boot Loader Section --
 *    not installed yet, see docs/bootloader.md's "deferred" section), and
 *  - the pre-ISP validation test harness (bootloader/test_harness_main.c
 *    -> boot_main(), linked with the existing boot/start.S + linker.ld,
 *    flashed via the existing, safe `make flash` path).
 *
 * boot_main() never hardcodes a destination flash address -- the incoming
 * KERNEL_INFO/PAGE packets carry it -- which is exactly what lets the same
 * logic be exercised safely against a scratch region by the harness before
 * ever running against the real Boot Loader Section. */
void boot_main(void);

#endif /* MYOS_BOOT_MAIN_H */
