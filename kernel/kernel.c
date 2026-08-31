#include <avr/pgmspace.h>
#include "drivers/uart.h"

void kernel_main(void)
{
    uart_init();

    uart_puts_P(PSTR("====================================\r\n"));
    uart_puts_P(PSTR("        MyOS v0.1\r\n"));
    uart_puts_P(PSTR("====================================\r\n"));
    uart_puts_P(PSTR("\r\n"));
    uart_puts_P(PSTR("ATmega328P kernel started.\r\n"));
    uart_puts_P(PSTR("\r\n"));
    uart_puts_P(PSTR("myos>"));

    for (;;) {
    }
}
