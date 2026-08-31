#include <stddef.h>
#include "task.h"
#include "kernel/timer.h"
#include "drivers/gpio.h"
#include "shell/shell.h"

/* Onboard LED pin on the Arduino Uno (matches drivers/gpio.h's pin
 * numbering: D13, PORTB bit 5). */
#define LED_PIN 13

/* Blink LED toggling every N ticks; at the established 1kHz tick rate
 * (see kernel/timer.c) that's roughly 500ms per state. */
#define LED_TOGGLE_TICKS 500

/* Fixed-size table of tasks, file-local static, mirroring kernel/memory.c's
 * block-list pattern: the array itself is never exposed directly, only
 * through task_count()/task_get(). */
#define MAX_TASKS 3

static task_t task_table[MAX_TASKS];

void task_init(void)
{
    task_table[0].id = 1;
    task_table[0].state = TASK_STATE_READY;
    task_table[0].entry = shell_run;

    task_table[1].id = 2;
    task_table[1].state = TASK_STATE_READY;
    task_table[1].entry = task_led;

    task_table[2].id = 3;
    task_table[2].state = TASK_STATE_READY;
    task_table[2].entry = task_system_service;
}

unsigned char task_count(void)
{
    return MAX_TASKS;
}

const task_t *task_get(unsigned char index)
{
    if (index >= MAX_TASKS) {
        return NULL;
    }

    return &task_table[index];
}

/* Genuinely working LED blink loop: toggles the LED every
 * LED_TOGGLE_TICKS ticks, busy-polling timer_get_ticks() rather than
 * blocking-sleeping (there is no sleep primitive). This is correct and
 * complete as a standalone function; nothing currently calls it, since
 * there is no scheduler yet to invoke a task's entry point. Milestone 12
 * will be the one that adapts it to cooperatively yield instead of only
 * being callable in isolation. */
void task_led(void)
{
    unsigned long last_toggle = timer_get_ticks();
    unsigned char led_on = 0;

    for (;;) {
        unsigned long now = timer_get_ticks();

        if ((unsigned long)(now - last_toggle) >= LED_TOGGLE_TICKS) {
            last_toggle = now;
            led_on = !led_on;

            if (led_on) {
                gpio_set(LED_PIN);
            } else {
                gpio_clear(LED_PIN);
            }
        }
    }
}

/* Reserved for future system-level periodic work (e.g. watchdog petting
 * once Milestone 35 lands). Does nothing yet — the spec only names this
 * as the 3rd example task, it doesn't define its behavior at this
 * milestone. */
void task_system_service(void)
{
    for (;;) {
        /* Nothing to do yet. */
    }
}
