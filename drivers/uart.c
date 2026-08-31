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
