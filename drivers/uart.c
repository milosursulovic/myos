#include <avr/io.h>
#include <avr/pgmspace.h>
#include "uart.h"

#define UART_BAUD 9600UL

/* Datasheet USART section (Table "Examples of UBRRn Settings"): for
 * asynchronous normal mode (U2X0 = 0),
 *   UBRR = F_CPU / (16 * BAUD) - 1
 */
#define UART_UBRR ((F_CPU / (16UL * UART_BAUD)) - 1)

void uart_init(void)
{
    /* Set baud rate. */
    UBRR0H = (uint8_t)(UART_UBRR >> 8);
    UBRR0L = (uint8_t)(UART_UBRR & 0xFF);

    /* Enable receiver and transmitter. */
    UCSR0B = (1 << RXEN0) | (1 << TXEN0);

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
    /* Block until a byte has been received. */
    while (!(UCSR0A & (1 << RXC0))) {
    }
    return (char)UDR0;
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
