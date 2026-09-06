/* Pre-ISP validation harness (Milestone 14) -- see docs/bootloader.md for
 * the full rationale and how to run it.
 *
 * The real bootloader can't be installed without ISP hardware (writing to
 * the Boot Loader Section, with no serial-only recovery path if it goes
 * wrong -- see docs/bootloader.md's "deferred" section). This harness
 * exercises the EXACT SAME protocol + SPM logic (boot_main(), unmodified,
 * from boot_main.c) safely instead, by running it as an ordinary
 * *application* -- entered via the existing, already-proven
 * boot/start.S + linker.ld + `make flash`/Optiboot path, the same as the
 * kernel binary every earlier milestone has used.
 *
 * boot_main() never hardcodes a destination flash address -- it takes
 * base_addr/total_size/expected_crc from the KERNEL_INFO packet on every
 * run (see boot_main.c) -- so nothing in THIS file needs to know about
 * addresses at all. Pointing it at a safe scratch region instead of the
 * real kernel's own addresses (0x0000 upward) is entirely a host-side
 * choice: tools/myos-upload.c's optional third command-line argument
 * (base address, hex) is what the person running the test sets, not
 * anything compiled into this harness.
 *
 * Only extra thing this file needs to provide: boot/start.S's vector
 * table unconditionally `jmp`s to TIMER0_COMPA_vect and USART_RX_vect (it
 * has to -- those slots exist whether or not this particular binary ever
 * enables either interrupt), so both symbols must exist at link time or
 * the harness won't link. boot_main() never calls sei() and never enables
 * RXCIE0/OCIE0A, so neither of these ever actually runs -- they exist
 * purely to satisfy the linker, matching what boot/start.S's own
 * `bad_interrupt` trap says about "only OCIE0A/RXCIE0 are ever legitimate
 * sources": here, neither is ever set, so if one of these somehow did
 * fire it would itself be the bug. */

#include <avr/interrupt.h>
#include "boot_main.h"

ISR(TIMER0_COMPA_vect)
{
}

ISR(USART_RX_vect)
{
}

/* boot/start.S calls kernel_main() after SP init and .data/.bss
 * setup -- this harness's only job is to hand off to the shared
 * protocol logic immediately. */
void kernel_main(void)
{
    boot_main();
}
