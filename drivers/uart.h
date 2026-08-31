#ifndef MYOS_UART_H
#define MYOS_UART_H

/* USART0 driver: 9600 baud, 8 data bits, no parity, 1 stop bit (8N1).
 * Polling only — no interrupts (that's Milestone 9). */

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *str);      /* str lives in RAM (e.g. a buffer). */
void uart_puts_P(const char *str);    /* str lives in flash (PROGMEM/PSTR). */
char uart_getc(void);

#endif /* MYOS_UART_H */
