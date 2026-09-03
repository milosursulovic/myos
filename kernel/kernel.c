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

    /* Populates the task table so the shell's `tasks` command has
     * something to show, and so the scheduler below has tasks to
     * switch between. */
    task_init();

    /* Milestone 12/13: real multitasking. scheduler_init() builds task
     * 2/3's kmalloc()'d stacks and fake initial frames. This must finish
     * BEFORE interrupts go live: as of Milestone 13, TIMER0_COMPA_vect
     * itself drives a scheduler switch (see scheduler_tick() in
     * kernel/scheduler.c), reading/writing task_table with no locking of
     * its own beyond what the caller already guarantees. A Timer0 tick
     * firing mid-scheduler_init() -- e.g. between task 2's and task 3's
     * sp being set -- could switch into a half-initialized task table. */
    scheduler_init();

    /* Global interrupt enable, done deliberately here as its own step —
     * after every interrupt-driven subsystem is configured, including the
     * scheduler itself now that Milestone 13 made it one. Timer0 is
     * currently the only interrupt source enabled (see kernel/timer.c). */
    sei();

    /* scheduler_run() starts task 1 (the shell) directly and never
     * returns, same as the direct shell_run() call it replaces. */
    scheduler_run();
}
