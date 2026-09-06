/* MyOS custom bootloader protocol + flash self-programming logic
 * (Milestone 14, spec sections 29-31). See docs/bootloader.md for the full
 * packet format, flow diagram, and pre-ISP validation results.
 *
 * Polling-only, no interrupts, no vector table. Two independent reasons:
 *
 *  1. The AVR interrupt vector table lives at a *fixed* flash address
 *     (0x0000-0x0067ish) regardless of the BOOTRST fuse -- that address
 *     range is physically part of the *kernel's* own flash image, i.e.
 *     exactly the memory this bootloader might be in the middle of
 *     erasing/rewriting when an update is in progress. Relying on vectors
 *     that could be mid-overwrite (or belong to whatever kernel happened
 *     to be there before) is unsafe. Polling needs none of them.
 *  2. Interrupt-free code, plus boot_start.S's lack of .data/.bss
 *     copy-down (see its own comment), is what keeps this whole
 *     implementation simple enough to have a realistic shot at fitting the
 *     1KB Boot Loader Section budget.
 *
 * All UART routines below are this file's own tiny polling
 * implementation -- deliberately NOT a reuse of drivers/uart.c's
 * interrupt-driven ring buffer (which needs RXCIE0/an ISR/the vector
 * table, all ruled out above). Same register-level approach as the
 * kernel's original Milestone 2 UART driver, simplified back down to
 * pure polling.
 *
 * Flash writes use <avr/boot.h>'s boot_page_erase()/boot_page_fill()/
 * boot_page_write()/boot_spm_busy_wait() macros, not hand-rolled SPM
 * assembly. These are compile-time macros implementing the datasheet's
 * mandated timed SPM instruction sequence (same category as
 * <avr/interrupt.h>'s ISR() or <avr/pgmspace.h>'s pgm_read_byte(),
 * already used throughout this project) -- far lower-risk than
 * reimplementing that timing/sequencing by hand given the stakes (a bad
 * self-write here has no ISP-free recovery path once this code actually
 * lives in the Boot Loader Section).
 *
 * IMPORTANT CORRECTION to this milestone's planning notes: the plan
 * assumed SPM_PAGESIZE = 128 *words* = 256 bytes/page. That is wrong --
 * confirmed directly against /usr/lib/avr/include/avr/iom328p.h
 * (`#define SPM_PAGESIZE 128`) and avr-libc's own boot.h API-usage
 * example, which loops `for (i = 0; i < SPM_PAGESIZE; i += 2)` filling
 * one word (2 bytes) per iteration -- i.e. SPM_PAGESIZE is already a
 * *byte* count, and the ATmega328P's real flash page size is 64 words =
 * 128 bytes, not 256. This file uses the SPM_PAGESIZE macro everywhere
 * instead of a hardcoded literal specifically so it can never silently
 * disagree with the toolchain's own value again; tools/myos-upload.c's
 * host side hardcodes 128 with a comment pointing back here, since it has
 * no access to avr-libc's headers. */

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include "boot_main.h"

/* ---------------------------------------------------------------------
 * Minimal polling UART: 9600 8N1, matching the kernel's own baud
 * (drivers/uart.c) so the same host-side serial settings work for both
 * the normal Optiboot kernel flash and this bootloader.
 * ------------------------------------------------------------------- */

#define BOOT_UART_BAUD 9600UL
#define BOOT_UART_UBRR ((F_CPU / (16UL * BOOT_UART_BAUD)) - 1)

static void boot_uart_init(void)
{
    UBRR0H = (uint8_t)(BOOT_UART_UBRR >> 8);
    UBRR0L = (uint8_t)(BOOT_UART_UBRR & 0xFF);

    /* Receiver + transmitter only -- no RXCIE0. Polling only, see the
     * top-of-file comment for why interrupts are ruled out entirely
     * here. */
    UCSR0B = (uint8_t)((1 << RXEN0) | (1 << TXEN0));

    /* 8 data bits, no parity, 1 stop bit -- same frame format as
     * drivers/uart.c. */
    UCSR0C = (uint8_t)((1 << UCSZ01) | (1 << UCSZ00));
}

static void boot_uart_putc(uint8_t c)
{
    while (!(UCSR0A & (1 << UDRE0))) {
    }
    UDR0 = c;
}

/* Blocking receive with no timeout -- used for every byte once a
 * handshake is already underway (the host is actively mid-transfer, so a
 * byte should always show up "soon"; there is nothing sensible to fall
 * back to mid-packet anyway). */
static uint8_t boot_uart_getc(void)
{
    while (!(UCSR0A & (1 << RXC0))) {
    }
    return UDR0;
}

/* Bounded receive: polls for up to `loops` iterations for a byte to
 * arrive, used only for the initial "is a host even out there" wait.
 * This is a plain busy-loop counter, not a hardware timer -- deliberately
 * interrupt-free/timer-free to match the rest of this file. Its duration
 * is therefore approximate and CPU-cycle-based (roughly proportional to
 * F_CPU), not a calibrated wall-clock value -- an accepted simplification
 * for a one-shot "did anyone say hello" window, not something that needs
 * millisecond precision. See docs/bootloader.md for the rough figure this
 * was sized against. Returns 1 and stores the byte via `out` if one
 * arrived in time, 0 (with `out` unmodified) on timeout. */
static int boot_uart_getc_timeout(uint8_t *out, uint32_t loops)
{
    uint32_t i;

    for (i = 0; i < loops; i++) {
        if (UCSR0A & (1 << RXC0)) {
            *out = UDR0;
            return 1;
        }
    }
    return 0;
}

/* Approximately 1-2 seconds at 16MHz -- see the function comment above
 * and docs/bootloader.md for how this was sized. */
#define BOOT_HELLO_TIMEOUT_LOOPS 1500000UL

/* ---------------------------------------------------------------------
 * CRC-8, polynomial 0x07 (CRC-8-CCITT/CRC-8-ATM), no reflection, initial
 * value 0. Chosen over CRC-16 for smaller/simpler code on the
 * 1KB-constrained device side -- more than adequate for catching
 * transmission errors on packets this small (<=128-byte payload) and for
 * a final whole-image sanity check after every page has already been
 * individually ACKed. Must be implemented identically here and in
 * tools/myos-upload.c's host side -- see that file's matching crc8().
 * ------------------------------------------------------------------- */

static uint8_t crc8_update(uint8_t crc, uint8_t byte)
{
    uint8_t bit;

    crc ^= byte;
    for (bit = 0; bit < 8; bit++) {
        if (crc & 0x80) {
            crc = (uint8_t)((crc << 1) ^ 0x07);
        } else {
            crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* ---------------------------------------------------------------------
 * Packet format (docs/bootloader.md has the full spec):
 *   MAGIC(2) | TYPE(1) | LENGTH(2, little-endian) | SEQUENCE(1) |
 *   PAYLOAD(<=256, LENGTH bytes of it are meaningful) | CRC(1)
 * CRC-8 above is computed over TYPE, LENGTH, SEQUENCE, and PAYLOAD only
 * (not MAGIC, not itself).
 *
 * For PAGE packets specifically, SEQUENCE doubles as the page number
 * (0-based) and PAYLOAD is exactly SPM_PAGESIZE (128) bytes of that
 * page's data -- reusing the header's existing SEQUENCE field instead of
 * also encoding a page number inside the payload, which keeps PAYLOAD
 * exactly the raw page bytes with nothing else to unpack.
 * ------------------------------------------------------------------- */

#define BOOT_MAGIC0 0xA5
#define BOOT_MAGIC1 0x5A

enum {
    PKT_HELLO = 0x01,
    PKT_READY = 0x02,
    PKT_KERNEL_INFO = 0x03,
    PKT_OK = 0x04,
    PKT_PAGE = 0x05,
    PKT_ACK = 0x06,
    PKT_VERIFY = 0x07,
    PKT_ERROR = 0x08,
    PKT_BOOT = 0x09
};

/* Fixed 256-byte payload buffer, matching the packet format's stated max
 * -- PAGE packets only ever use the first SPM_PAGESIZE (128) bytes of
 * it, but sizing the buffer to the protocol's general limit (rather than
 * to SPM_PAGESIZE specifically) keeps this struct usable for any future
 * packet type without changing it again. */
typedef struct {
    uint8_t type;
    uint16_t length;
    uint8_t seq;
    uint8_t payload[256];
} boot_packet_t;

static void send_packet(uint8_t type, const uint8_t *payload, uint16_t length, uint8_t seq)
{
    uint8_t crc = 0;
    uint16_t i;

    boot_uart_putc(BOOT_MAGIC0);
    boot_uart_putc(BOOT_MAGIC1);

    boot_uart_putc(type);
    crc = crc8_update(crc, type);

    boot_uart_putc((uint8_t)(length & 0xFF));
    crc = crc8_update(crc, (uint8_t)(length & 0xFF));
    boot_uart_putc((uint8_t)(length >> 8));
    crc = crc8_update(crc, (uint8_t)(length >> 8));

    boot_uart_putc(seq);
    crc = crc8_update(crc, seq);

    for (i = 0; i < length; i++) {
        boot_uart_putc(payload[i]);
        crc = crc8_update(crc, payload[i]);
    }

    boot_uart_putc(crc);
}

/* Reads TYPE/LENGTH/SEQUENCE/PAYLOAD/CRC, assuming the 2 MAGIC bytes have
 * already been consumed by the caller. Returns 1 with `pkt` filled in if
 * the CRC checks out, 0 otherwise (bad CRC, or a LENGTH too large for
 * `pkt->payload` to hold). */
static int recv_packet_body(boot_packet_t *pkt)
{
    uint8_t lenlo, lenhi;
    uint8_t crc, crc_recv;
    uint16_t i;

    pkt->type = boot_uart_getc();
    crc = crc8_update(0, pkt->type);

    lenlo = boot_uart_getc();
    crc = crc8_update(crc, lenlo);
    lenhi = boot_uart_getc();
    crc = crc8_update(crc, lenhi);
    pkt->length = (uint16_t)((uint16_t)lenlo | ((uint16_t)lenhi << 8));

    pkt->seq = boot_uart_getc();
    crc = crc8_update(crc, pkt->seq);

    if (pkt->length > sizeof(pkt->payload)) {
        return 0;
    }

    for (i = 0; i < pkt->length; i++) {
        pkt->payload[i] = boot_uart_getc();
        crc = crc8_update(crc, pkt->payload[i]);
    }

    crc_recv = boot_uart_getc();
    return crc == crc_recv;
}

/* Blocks (no timeout) until a full, CRC-valid packet arrives. Used for
 * every packet after the initial handshake, where the host is known to
 * be actively transferring. */
static int recv_packet(boot_packet_t *pkt)
{
    if (boot_uart_getc() != BOOT_MAGIC0) {
        return 0;
    }
    if (boot_uart_getc() != BOOT_MAGIC1) {
        return 0;
    }
    return recv_packet_body(pkt);
}

/* Bounded wait for a HELLO: only the very first magic byte is subject to
 * the timeout (nothing has arrived yet, might be nothing ever does); once
 * that byte looks like the start of a real packet, the rest is read with
 * the same unbounded recv_packet_body() as everything else (a host that
 * has started sending is assumed to keep sending promptly). Returns 1 if
 * a valid HELLO packet was received, 0 on timeout or any malformed/
 * unexpected packet (both treated the same way by the caller: give up
 * and boot the existing kernel). */
static int wait_for_hello(void)
{
    boot_packet_t pkt;
    uint8_t b;

    if (!boot_uart_getc_timeout(&b, BOOT_HELLO_TIMEOUT_LOOPS)) {
        return 0;
    }
    if (b != BOOT_MAGIC0) {
        return 0;
    }
    /* KNOWN, ACCEPTED LIMITATION (not fixed -- see docs/bootloader.md):
     * this next read is unbounded (boot_uart_getc(), not a timeout
     * variant), same as recv_packet_body() below. A single stray/noise
     * byte on the line that happens to equal MAGIC0, with no real MAGIC1
     * ever following, would wedge this function waiting forever instead
     * of falling through to jump_to_kernel() as intended. A bounded
     * version was tried and worked, but pushed build/bootloader.elf over
     * the 1KB Boot Loader Section budget (1026 vs 1024 bytes available)
     * -- given how narrow this specific noise scenario is in practice,
     * staying within the hard size budget wins over closing it. */
    if (boot_uart_getc() != BOOT_MAGIC1) {
        return 0;
    }
    if (!recv_packet_body(&pkt)) {
        return 0;
    }
    return pkt.type == PKT_HELLO;
}

/* Like wait_for_hello(), but NEVER gives up -- used for every resync
 * point once an update session has actually started (i.e. once a
 * KERNEL_INFO has been accepted and this device may already have begun
 * overwriting base_addr's memory). From that point on, the ONLY way this
 * bootloader will ever boot anything is a successfully-verified, explicit
 * BOOT command -- falling back to jump_to_kernel() on a mere timeout
 * would risk booting a partially-written or corrupt image, which is
 * exactly what this whole milestone exists to prevent. If the host goes
 * away for good mid-update, the user's recovery path is simply to
 * reset/power-cycle the board, which re-enters boot_main() and its ONE
 * pre-session wait_for_hello() call, still allowed to time out and boot
 * normally since nothing has been touched yet at that point. */
static void wait_for_hello_no_boot(void)
{
    for (;;) {
        if (wait_for_hello()) {
            return;
        }
    }
}

/* ---------------------------------------------------------------------
 * Flash self-programming and final verification.
 * ------------------------------------------------------------------- */

/* Erase + fill + write exactly one SPM_PAGESIZE-byte page at `addr`
 * (which must be page-aligned -- callers derive it as
 * base + page_number * SPM_PAGESIZE, always a multiple of SPM_PAGESIZE).
 * Matches the erase -> busy_wait -> repeated boot_page_fill two bytes at
 * a time -> boot_page_write -> busy_wait sequence from avr-libc's own
 * <avr/boot.h> documentation, with one deliberate omission: the
 * documented example also calls eeprom_busy_wait() before boot_page_erase
 * (guarding against the shared NVM controller being mid-EEPROM-write).
 * Currently safe to omit -- this project has no EEPROM driver at all yet
 * (nothing can be mid-write) -- but add that call back if one is ever
 * added before this bootloader is finalized. boot_rww_enable() is
 * deliberately NOT called here per page -- see its single call after the
 * whole transfer loop in boot_main(), matching that same canonical
 * example. */
static void write_page(uint32_t addr, const uint8_t *data)
{
    uint16_t i;
    uint16_t w;

    boot_page_erase(addr);
    boot_spm_busy_wait();

    for (i = 0; i < SPM_PAGESIZE; i += 2) {
        w = (uint16_t)((uint16_t)data[i] | ((uint16_t)data[i + 1] << 8));
        boot_page_fill(addr + i, w);
    }

    boot_page_write(addr);
    boot_spm_busy_wait();
}

/* CRC-8 over `length` bytes of flash starting at `addr`, read back via
 * pgm_read_byte() (an LPM read) -- i.e. this reads the ACTUAL just-written
 * flash contents, not the in-memory copy that was sent, so a genuine SPM
 * failure would be caught here. Caller must have already called
 * boot_rww_enable() if `addr` falls in the RWW section (see boot_main()). */
static uint8_t crc8_flash_region(uint16_t addr, uint32_t length)
{
    uint8_t crc = 0;
    uint32_t i;

    for (i = 0; i < length; i++) {
        crc = crc8_update(crc, pgm_read_byte((uint16_t)(addr + i)));
    }
    return crc;
}

/* Jumps to the application section's reset address (0x0000). A plain
 * function-pointer call to a fixed address, per the spec's flow diagram
 * -- deliberately not relying on anything living at 0x0000 being a
 * "vector table" (this bootloader has none of its own and makes no
 * assumption about what's already there beyond "the kernel's own reset
 * entry point"). Never returns. */
static void jump_to_kernel(void)
{
    void (*kernel_entry)(void) = (void (*)(void))0x0000;
    kernel_entry();
}

/* ---------------------------------------------------------------------
 * Top-level flow (spec section 29):
 *   RESET -> UART init -> wait for host (HELLO, bounded) ->
 *     timeout -> boot existing kernel (the ONLY timeout->boot path in
 *                this whole file -- see wait_for_hello_no_boot() above)
 *     HELLO received -> READY -> KERNEL_INFO -> OK ->
 *       PAGE (repeat) -> ACK (repeat) ->
 *       VERIFY -> OK -> BOOT -> boot new kernel
 *       VERIFY -> ERROR -> resync, wait (unbounded) for a fresh HELLO
 *                           (never boots a kernel known to have failed
 *                           verification)
 * Any other malformed/out-of-order packet once a KERNEL_INFO has been
 * accepted resyncs the same way (unbounded wait), never via a timeout
 * that could fall through to booting a base_addr region this device may
 * have already started overwriting.
 * ------------------------------------------------------------------- */

void boot_main(void)
{
    boot_packet_t pkt;
    uint16_t base_addr;
    uint32_t total_size;
    uint8_t expected_crc;
    uint32_t num_pages;
    uint32_t p;
    uint32_t page_addr;
    uint8_t actual_crc;
    int transfer_ok;

    boot_uart_init();

    /* The ONE and only wait allowed to time out straight into
     * jump_to_kernel() -- nothing has been touched yet at this point, so
     * "nobody's updating me" and "boot normally" are the same thing. */
    if (!wait_for_hello()) {
        jump_to_kernel();
        /* never returns */
    }

    for (;;) {
        send_packet(PKT_READY, 0, 0, 0);

        /* KERNEL_INFO payload: base_addr(2, LE) | total_size(4, LE) |
         * expected_crc(1). base_addr is what makes this file
         * address-agnostic -- it never hardcodes 0x0000, 0x2000 (the
         * test harness's scratch region), or 0x7C00 anywhere; the host
         * tells it where to write on every run. */
        if (!recv_packet(&pkt) || pkt.type != PKT_KERNEL_INFO || pkt.length != 7) {
            wait_for_hello_no_boot();
            continue;
        }
        base_addr = (uint16_t)((uint16_t)pkt.payload[0] | ((uint16_t)pkt.payload[1] << 8));
        total_size = (uint32_t)pkt.payload[2] | ((uint32_t)pkt.payload[3] << 8) |
                     ((uint32_t)pkt.payload[4] << 16) | ((uint32_t)pkt.payload[5] << 24);
        expected_crc = pkt.payload[6];

        /* Reject up front, before touching any flash: base_addr must be
         * page-aligned (an unaligned address would make boot_page_fill()
         * calls straddle two different physical hardware pages -- the
         * page write only commits the physical page containing the
         * address, silently losing/misplacing the rest), and the whole
         * [base_addr, base_addr+total_size) range must fit within this
         * device's real 32KB flash (FLASHEND=0x7FFF) -- SPM only decodes
         * the low bits of the address, so an out-of-range address could
         * silently alias back into low flash (the live vector table/
         * kernel image) instead of failing cleanly. */
        if (base_addr >= 0x8000U ||
            (base_addr % SPM_PAGESIZE) != 0 ||
            total_size > (uint32_t)(0x8000UL - base_addr)) {
            send_packet(PKT_ERROR, 0, 0, 0);
            wait_for_hello_no_boot();
            continue;
        }

        send_packet(PKT_OK, 0, 0, 0);

        /* From here on this device may be actively overwriting
         * base_addr's memory -- every resync below uses
         * wait_for_hello_no_boot(), never a path that could time out
         * into jump_to_kernel(). */

        num_pages = (total_size + SPM_PAGESIZE - 1) / SPM_PAGESIZE;
        transfer_ok = 1;

        for (p = 0; p < num_pages; p++) {
            if (!recv_packet(&pkt) || pkt.type != PKT_PAGE || pkt.length != SPM_PAGESIZE ||
                pkt.seq != (uint8_t)p) {
                transfer_ok = 0;
                break;
            }

            page_addr = base_addr + p * SPM_PAGESIZE;
            write_page(page_addr, pkt.payload);

            send_packet(PKT_ACK, 0, 0, pkt.seq);
        }

        if (!transfer_ok) {
            wait_for_hello_no_boot();
            continue;
        }

        /* Re-enable reading the RWW (application) section after writing
         * it from here (the boot loader / NRWW side) -- required before
         * the CRC verification's pgm_read_byte() calls below, and before
         * ever jumping into that freshly-written code. Matches
         * avr-libc's own <avr/boot.h> API-usage example, which also
         * calls this exactly once after the whole page-write loop. */
        boot_rww_enable();

        if (!recv_packet(&pkt) || pkt.type != PKT_VERIFY) {
            wait_for_hello_no_boot();
            continue;
        }

        actual_crc = crc8_flash_region(base_addr, total_size);
        if (actual_crc == expected_crc) {
            send_packet(PKT_OK, 0, 0, 0);
        } else {
            send_packet(PKT_ERROR, 0, 0, 0);
            wait_for_hello_no_boot(); /* never boot a kernel that failed verification */
            continue;
        }

        if (!recv_packet(&pkt) || pkt.type != PKT_BOOT) {
            wait_for_hello_no_boot();
            continue;
        }

        jump_to_kernel();
        /* never returns */
    }
}
