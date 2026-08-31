#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "uart.h"

#define UART_BAUD 9600UL

/* Datasheet USART section (Table "Examples of UBRRn Settings"): for
 * asynchronous normal mode (U2X0 = 0),
 *   UBRR = F_CPU / (16 * BAUD) - 1
 */
#define UART_UBRR ((F_CPU / (16UL * UART_BAUD)) - 1)

/* RX ring buffer (Milestone 9): USART_RX_vect (the producer) writes at
 * rx_head, uart_getc() (the consumer) reads from rx_tail. Size must be a
 * power of 2 — index wraparound is done with `& (RX_BUF_SIZE - 1)` instead
 * of `% RX_BUF_SIZE`, which is only equivalent to modulo for a power-of-2
 * size (it works because masking off the high bits of a power-of-2 divisor
 * discards exactly one full period).
 *
 * 64 bytes (63 usable — one slot is always kept empty to distinguish full
 * from empty). Bumped up from an initial 16 after hardware testing showed
 * that pasting several full commands into the shell at once (no gap
 * between them) can overflow a 16-byte buffer: TX is still polling/blocking
 * (see uart_putc()), so while the shell is busy printing one command's
 * response, it isn't calling uart_getc() at all, and more input keeps
 * arriving via the RX ISR in the meantime. A 140-byte response (the
 * longest — "info") takes ~140ms to transmit at 9600 baud, during which
 * ~140 bytes of pasted input could theoretically arrive; 64 bytes doesn't
 * cover that absolute worst case, but comfortably covers realistic
 * multi-command pastes and normal typing, at a modest 48-byte extra SRAM
 * cost. The only complete fix would be making TX interrupt-driven too
 * (out of Milestone 9's scope, which is RX-only per the spec) — this
 * remains a known, accepted limitation, not something eliminated. */
#define RX_BUF_SIZE 64
#define RX_BUF_MASK (RX_BUF_SIZE - 1)

static volatile unsigned char rx_buf[RX_BUF_SIZE];
static volatile unsigned char rx_head; /* next slot the ISR will write */
static volatile unsigned char rx_tail; /* next slot uart_getc() will read */

void uart_init(void)
{
    /* Set baud rate. */
    UBRR0H = (uint8_t)(UART_UBRR >> 8);
    UBRR0L = (uint8_t)(UART_UBRR & 0xFF);

    /* Enable receiver, transmitter, and the RX Complete interrupt. */
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);

    /* Frame format: 8 data bits, no parity, 1 stop bit (8N1). */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void uart_putc(char c)
{
    /* Wait until the transmit data register is empty. */
    while (!(UCSR0A & (1 << UDRE0))) {
    }
    UDR0 = c;
}

void uart_puts(const char *str)
{
    while (*str) {
        uart_putc(*str++);
    }
}

void uart_puts_P(const char *str)
{
    char c;
    while ((c = pgm_read_byte(str++))) {
        uart_putc(c);
    }
}

char uart_getc(void)
{
    unsigned char c;

    /* Block until the ISR has placed a byte in the ring buffer. Must NOT
     * disable interrupts while waiting — the ISR is what advances rx_head,
     * so masking interrupts here would hang forever on an empty buffer.
     *
     * rx_head and rx_tail are single unsigned char values, not multi-byte
     * like timer.c's system_ticks — on an 8-bit CPU a single-byte load or
     * store is one instruction and can't be torn by an interrupt landing
     * mid-access. This is a classic single-producer (ISR writes rx_head),
     * single-consumer (this function reads/writes rx_tail) lock-free ring
     * buffer, so no cli()/SREG masking is needed here. */
    while (rx_head == rx_tail) {
    }

    c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) & RX_BUF_MASK;
    return (char)c;
}

ISR(USART_RX_vect)
{
    unsigned char c = UDR0; /* also clears RXC0 — must always be read */
    unsigned char next_head = (rx_head + 1) & RX_BUF_MASK;

    /* Drop the byte silently if the buffer is full. There's no
     * error-reporting channel back to the sender, and an ISR can't block
     * waiting for uart_getc() to make room. */
    if (next_head != rx_tail) {
        rx_buf[rx_head] = c;
        rx_head = next_head;
    }
}

void uart_put_uint(unsigned int n)
{
    /* Manual decimal conversion (no snprintf/itoa — avr-libc isn't linked).
     * Digits come out least-significant-first, so they're buffered and
     * then emitted in reverse. Widest 16-bit value is "65535" (5 digits). */
    char digits[5];
    unsigned char i = 0;

    if (n == 0) {
        uart_putc('0');
        return;
    }

    while (n > 0) {
        digits[i++] = (char)('0' + (n % 10));
        n /= 10;
    }

    while (i > 0) {
        uart_putc(digits[--i]);
    }
}

void uart_put_ulong(unsigned long n)
{
    /* Same manual decimal conversion as uart_put_uint(), sized for a
     * 32-bit value. Widest is "4294967295" (10 digits). */
    char digits[10];
    unsigned char i = 0;

    if (n == 0) {
        uart_putc('0');
        return;
    }

    while (n > 0) {
        digits[i++] = (char)('0' + (n % 10));
        n /= 10;
    }

    while (i > 0) {
        uart_putc(digits[--i]);
    }
}
