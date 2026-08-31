#include <avr/interrupt.h>
#include "drivers/gpio.h"
#include "drivers/uart.h"
#include "kernel/timer.h"
#include "kernel/task.h"
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

    /* Populates the task table so the shell's `tasks` command has
     * something to show. There is no scheduler yet (Milestone 12) — this
     * does not change control flow below; shell_run() is still called
     * directly and still never returns. */
    task_init();

    shell_run();
}
