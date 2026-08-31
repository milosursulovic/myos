#include <avr/interrupt.h>
#include "drivers/gpio.h"
#include "drivers/uart.h"
#include "kernel/timer.h"
#include "shell/shell.h"

void kernel_main(void)
{
    uart_init();
    gpio_init();
    timer_init();

    /* Global interrupt enable, done deliberately here as its own step —
     * after every interrupt-driven subsystem is configured, not hidden
     * inside a driver's init function. Timer0 is currently the only
     * interrupt source enabled (see kernel/timer.c). */
    sei();

    shell_run();
}
