#include <avr/interrupt.h>
#include "drivers/gpio.h"
#include "drivers/uart.h"
#include "kernel/timer.h"
#include "kernel/task.h"
#include "kernel/scheduler.h"

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
     * something to show, and so the scheduler below has tasks to
     * switch between. */
    task_init();

    /* Milestone 12: real cooperative multitasking. scheduler_init()
     * builds task 2/3's kmalloc()'d stacks and fake initial frames;
     * scheduler_run() starts task 1 (the shell) directly and never
     * returns, same as the direct shell_run() call it replaces. */
    scheduler_init();
    scheduler_run();
}
