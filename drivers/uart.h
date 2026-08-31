#ifndef MYOS_UART_H
#define MYOS_UART_H

/* USART0 driver: 9600 baud, 8 data bits, no parity, 1 stop bit (8N1).
 * RX is interrupt-driven via a ring buffer (Milestone 9) behind
 * uart_getc()'s same blocking-until-a-byte-is-available contract. TX
 * (uart_putc() and everything built on it) is still polling on UDRE0. */

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *str);      /* str lives in RAM (e.g. a buffer). */
void uart_puts_P(const char *str);    /* str lives in flash (PROGMEM/PSTR). */
char uart_getc(void);
void uart_put_uint(unsigned int n);   /* prints n as decimal ASCII digits. */
void uart_put_ulong(unsigned long n); /* prints n as decimal ASCII digits. */

#endif /* MYOS_UART_H */
