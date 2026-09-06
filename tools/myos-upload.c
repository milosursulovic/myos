/*
 * myos-upload -- PC-side host tool for MyOS's custom update protocol
 * (Milestone 14, spec section 31). See docs/bootloader.md for the full
 * packet format and protocol flow; this implements the host half of
 * exactly the same state machine bootloader/boot_main.c implements on
 * the device side.
 *
 * This is PC-side tooling, not AVR target code, so it is deliberately
 * plain C compiled with the system's `cc` (see the `tools` Makefile
 * target) -- CLAUDE.md's "no Arduino framework" rule is about what runs
 * ON the ATmega328P, not about host-side developer tools, and nothing
 * here touches AVR registers or runs on the target at all.
 *
 * Usage:
 *   myos-upload <serial-port> <kernel-image.bin> [base-address-hex]
 *
 * <serial-port>      e.g. /dev/ttyACM0
 * <kernel-image.bin> a raw binary image (build/myos.bin, NOT the .hex),
 *                     see the `myos-bin` Makefile target.
 * [base-address-hex]  OPTIONAL, defaults to 0x0000 (the real deployment
 *                     target -- the start of the application section a
 *                     real bootloader install would overwrite). This is
 *                     an addition beyond the two required arguments the
 *                     protocol otherwise needs, specifically so this same
 *                     tool can also drive the pre-ISP validation test
 *                     harness (bootloader/test_harness_main.c) at a safe
 *                     SCRATCH address instead of 0x0000 -- see
 *                     docs/bootloader.md's validation section for the
 *                     exact address used and why it's safe. Format:
 *                     plain hex, with or without a leading "0x"
 *                     (e.g. 2000 or 0x2000).
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

/* Must match bootloader/boot_main.c's SPM_PAGESIZE exactly -- the
 * ATmega328P's real flash page size (confirmed via avr-libc's
 * SPM_PAGESIZE macro and its own <avr/boot.h> API-usage example) is 64
 * words = 128 BYTES, not 256 as an early planning pass for this milestone
 * mistakenly assumed. This side has no access to avr-libc's headers
 * (it's compiled with the system cc, not avr-gcc), so the value is
 * hardcoded here with this comment as the cross-reference back to the
 * authoritative device-side definition. */
#define BOOT_PAGE_SIZE 128

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

typedef struct {
    uint8_t type;
    uint16_t length;
    uint8_t seq;
    uint8_t payload[256];
} packet_t;

/* CRC-8, polynomial 0x07 (CRC-8-CCITT/CRC-8-ATM), no reflection, initial
 * value 0 -- must stay byte-for-byte identical to
 * bootloader/boot_main.c's crc8_update(). */
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

static int open_serial_port(const char *path)
{
    int fd;
    struct termios tio;

    fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "Error: failed to open serial port '%s': %s\n", path, strerror(errno));
        return -1;
    }

    if (tcgetattr(fd, &tio) != 0) {
        fprintf(stderr, "Error: tcgetattr('%s') failed: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, B9600);
    cfsetospeed(&tio, B9600);

    /* 8N1, matching the bootloader's own frame format. cfmakeraw()
     * already disables canonical mode/echo/signal generation, but the
     * character size/parity/stop bits still need to be set explicitly. */
    tio.c_cflag &= ~(unsigned int)(PARENB | CSTOPB | CSIZE);
    tio.c_cflag |= CS8 | CLOCAL | CREAD;

    /* Blocking reads of at least 1 byte, no inter-byte timer -- this
     * tool's own read_byte_timeout() below layers a real per-byte
     * timeout on top via select(), so VMIN/VTIME just need to not get in
     * the way. */
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        fprintf(stderr, "Error: tcsetattr('%s') failed: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

static void write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    ssize_t n;

    while (off < len) {
        n = write(fd, buf + off, len - off);
        if (n < 0) {
            fprintf(stderr, "Error: write to serial port failed: %s\n", strerror(errno));
            exit(1);
        }
        off += (size_t)n;
    }
}

/* Blocks for at most timeout_ms waiting for one byte. Returns 1 and
 * stores it via *out on success, 0 on timeout or read error. */
static int read_byte_timeout(int fd, uint8_t *out, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;
    int ret;
    ssize_t n;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (ret <= 0) {
        return 0;
    }
    n = read(fd, out, 1);
    return n == 1;
}

static void send_packet(int fd, uint8_t type, const uint8_t *payload, uint16_t length, uint8_t seq)
{
    uint8_t buf[2 + 1 + 2 + 1 + 256 + 1];
    size_t pos = 0;
    uint8_t crc = 0;
    uint16_t i;

    buf[pos++] = BOOT_MAGIC0;
    buf[pos++] = BOOT_MAGIC1;

    buf[pos] = type;
    crc = crc8_update(crc, buf[pos]);
    pos++;

    buf[pos] = (uint8_t)(length & 0xFF);
    crc = crc8_update(crc, buf[pos]);
    pos++;
    buf[pos] = (uint8_t)(length >> 8);
    crc = crc8_update(crc, buf[pos]);
    pos++;

    buf[pos] = seq;
    crc = crc8_update(crc, buf[pos]);
    pos++;

    for (i = 0; i < length; i++) {
        buf[pos] = payload[i];
        crc = crc8_update(crc, buf[pos]);
        pos++;
    }

    buf[pos++] = crc;

    write_all(fd, buf, pos);
}

/* Receives one full, CRC-checked packet, giving each individual byte up
 * to timeout_ms to arrive. Returns 1 on a good packet, 0 on timeout, a
 * bad magic, or a CRC mismatch. */
static int recv_packet_timeout(int fd, packet_t *pkt, int timeout_ms)
{
    uint8_t b, lenlo, lenhi, crc, crc_recv;
    uint16_t i;

    if (!read_byte_timeout(fd, &b, timeout_ms) || b != BOOT_MAGIC0) {
        return 0;
    }
    if (!read_byte_timeout(fd, &b, timeout_ms) || b != BOOT_MAGIC1) {
        return 0;
    }

    if (!read_byte_timeout(fd, &pkt->type, timeout_ms)) {
        return 0;
    }
    crc = crc8_update(0, pkt->type);

    if (!read_byte_timeout(fd, &lenlo, timeout_ms)) {
        return 0;
    }
    crc = crc8_update(crc, lenlo);
    if (!read_byte_timeout(fd, &lenhi, timeout_ms)) {
        return 0;
    }
    crc = crc8_update(crc, lenhi);
    pkt->length = (uint16_t)((uint16_t)lenlo | ((uint16_t)lenhi << 8));

    if (!read_byte_timeout(fd, &pkt->seq, timeout_ms)) {
        return 0;
    }
    crc = crc8_update(crc, pkt->seq);

    if (pkt->length > sizeof(pkt->payload)) {
        return 0;
    }

    for (i = 0; i < pkt->length; i++) {
        if (!read_byte_timeout(fd, &pkt->payload[i], timeout_ms)) {
            return 0;
        }
        crc = crc8_update(crc, pkt->payload[i]);
    }

    if (!read_byte_timeout(fd, &crc_recv, timeout_ms)) {
        return 0;
    }
    return crc == crc_recv;
}

/* Sends HELLO and retries a few times, since opening a USB-serial port to
 * an Arduino Uno commonly toggles DTR and resets the MCU right as the
 * port opens -- the bootloader's own wait-for-HELLO window
 * (BOOT_HELLO_TIMEOUT_LOOPS, ~1-2s, see boot_main.c) may already be
 * partway elapsed, or not even started yet, by the time this process
 * gets scheduled and opens the port. Several short retries comfortably
 * covers that race without a long fixed sleep. */
static int handshake(int fd)
{
    packet_t pkt;
    int attempt;

    for (attempt = 0; attempt < 8; attempt++) {
        send_packet(fd, PKT_HELLO, NULL, 0, 0);
        if (recv_packet_timeout(fd, &pkt, 400) && pkt.type == PKT_READY) {
            return 1;
        }
    }
    return 0;
}

static uint8_t crc8_buffer(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    size_t i;

    for (i = 0; i < len; i++) {
        crc = crc8_update(crc, data[i]);
    }
    return crc;
}

static uint8_t *read_whole_file(const char *path, size_t *out_size)
{
    FILE *f;
    long size;
    uint8_t *buf;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: failed to open image file '%s': %s\n", path, strerror(errno));
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error: failed to determine size of '%s'\n", path);
        fclose(f);
        return NULL;
    }

    buf = malloc((size_t)size);
    if (!buf) {
        fprintf(stderr, "Error: out of memory reading '%s' (%ld bytes)\n", path, size);
        fclose(f);
        return NULL;
    }

    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "Error: short read on '%s'\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *out_size = (size_t)size;
    return buf;
}

int main(int argc, char **argv)
{
    const char *port_path;
    const char *image_path;
    unsigned long base_addr = 0x0000UL;
    int fd;
    uint8_t *image;
    size_t image_size;
    uint8_t expected_crc;
    uint32_t num_pages;
    uint32_t p;
    packet_t pkt;
    uint8_t info_payload[7];
    uint8_t page_buf[BOOT_PAGE_SIZE];
    size_t offset;
    size_t chunk;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Usage: %s <serial-port> <kernel-image.bin> [base-address-hex]\n", argv[0]);
        return 1;
    }
    port_path = argv[1];
    image_path = argv[2];
    if (argc == 4) {
        base_addr = strtoul(argv[3], NULL, 16);
    }

    /* 0x8000 (32768) is the ATmega328P's real flash size (FLASHEND=0x7FFF),
     * not just the 16-bit address space -- checking only "<=0xFFFF" would
     * let an out-of-range address through, which SPM would silently alias
     * back into low flash (the live vector table/kernel image) rather than
     * failing cleanly. base_addr must also be page-aligned (BOOT_PAGE_SIZE,
     * 128 bytes) -- see bootloader/boot_main.c's matching device-side
     * check and comment for why an unaligned address is unsafe, not just
     * rejected for tidiness. */
    if (base_addr >= 0x8000UL) {
        fprintf(stderr, "Error: base address 0x%lX is outside the ATmega328P's real 32KB flash (0x0000-0x7FFF)\n", base_addr);
        return 1;
    }
    if ((base_addr % BOOT_PAGE_SIZE) != 0) {
        fprintf(stderr, "Error: base address 0x%lX is not page-aligned (must be a multiple of %d)\n", base_addr, BOOT_PAGE_SIZE);
        return 1;
    }

    image = read_whole_file(image_path, &image_size);
    if (!image) {
        return 1;
    }
    if (image_size == 0 || image_size > 0x8000) {
        fprintf(stderr, "Error: '%s' is %zu bytes, not a plausible kernel image (expected 1-32768 bytes)\n",
                image_path, image_size);
        free(image);
        return 1;
    }
    if (base_addr + image_size > 0x8000UL) {
        fprintf(stderr, "Error: base address 0x%lX + image size %zu bytes exceeds the 32KB flash end (0x8000)\n",
                base_addr, image_size);
        free(image);
        return 1;
    }

    expected_crc = crc8_buffer(image, image_size);
    num_pages = (uint32_t)((image_size + BOOT_PAGE_SIZE - 1) / BOOT_PAGE_SIZE);

    printf("Image: %s (%zu bytes, %u pages of %d bytes, CRC-8 0x%02X)\n", image_path, image_size, num_pages,
           BOOT_PAGE_SIZE, expected_crc);
    printf("Target: base address 0x%04lX\n", base_addr);

    fd = open_serial_port(port_path);
    if (fd < 0) {
        free(image);
        return 1;
    }

    printf("Waiting for bootloader HELLO handshake on %s...\n", port_path);
    if (!handshake(fd)) {
        fprintf(stderr,
                "Error: timed out waiting for READY -- no bootloader responded. Check the "
                "port, that the board was just reset, and that it's running the MyOS "
                "bootloader (not Optiboot).\n");
        close(fd);
        free(image);
        return 1;
    }
    printf("Bootloader READY.\n");

    info_payload[0] = (uint8_t)(base_addr & 0xFF);
    info_payload[1] = (uint8_t)((base_addr >> 8) & 0xFF);
    info_payload[2] = (uint8_t)(image_size & 0xFF);
    info_payload[3] = (uint8_t)((image_size >> 8) & 0xFF);
    info_payload[4] = (uint8_t)((image_size >> 16) & 0xFF);
    info_payload[5] = (uint8_t)((image_size >> 24) & 0xFF);
    info_payload[6] = expected_crc;

    send_packet(fd, PKT_KERNEL_INFO, info_payload, sizeof(info_payload), 0);
    if (!recv_packet_timeout(fd, &pkt, 2000) || pkt.type != PKT_OK) {
        fprintf(stderr, "Error: bootloader did not acknowledge KERNEL_INFO\n");
        close(fd);
        free(image);
        return 1;
    }

    for (p = 0; p < num_pages; p++) {
        offset = (size_t)p * BOOT_PAGE_SIZE;
        chunk = image_size - offset;
        if (chunk > BOOT_PAGE_SIZE) {
            chunk = BOOT_PAGE_SIZE;
        }

        memcpy(page_buf, image + offset, chunk);
        /* Pad the tail of the last page with 0xFF, matching AVR's erased
         * (unprogrammed) flash value, so any padding bytes look exactly
         * like flash that was simply never written rather than like real
         * data. */
        if (chunk < BOOT_PAGE_SIZE) {
            memset(page_buf + chunk, 0xFF, BOOT_PAGE_SIZE - chunk);
        }

        send_packet(fd, PKT_PAGE, page_buf, BOOT_PAGE_SIZE, (uint8_t)p);

        if (!recv_packet_timeout(fd, &pkt, 2000) || pkt.type != PKT_ACK || pkt.seq != (uint8_t)p) {
            fprintf(stderr, "Error: did not receive ACK for page %u/%u\n", p + 1, num_pages);
            close(fd);
            free(image);
            return 1;
        }
        printf("Page %u/%u ACKed\n", p + 1, num_pages);
    }

    send_packet(fd, PKT_VERIFY, NULL, 0, 0);
    if (!recv_packet_timeout(fd, &pkt, 3000)) {
        fprintf(stderr, "Error: timed out waiting for VERIFY response\n");
        close(fd);
        free(image);
        return 1;
    }
    if (pkt.type == PKT_ERROR) {
        fprintf(stderr, "Error: bootloader reported a CRC mismatch after VERIFY -- flash write did not "
                        "round-trip correctly. Not sending BOOT.\n"
                        "The device is safe: it will not boot the incomplete image and is now waiting "
                        "indefinitely for a fresh upload attempt (it will NOT time out and boot on its "
                        "own from this point) -- just re-run this tool.\n");
        close(fd);
        free(image);
        return 1;
    }
    if (pkt.type != PKT_OK) {
        fprintf(stderr, "Error: unexpected response to VERIFY (type 0x%02X)\n", pkt.type);
        close(fd);
        free(image);
        return 1;
    }
    printf("Verify OK -- flash contents match the sent image.\n");

    send_packet(fd, PKT_BOOT, NULL, 0, 0);
    printf("Sent BOOT -- device should now be running the new image.\n");

    close(fd);
    free(image);
    return 0;
}
