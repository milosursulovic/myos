#ifndef MYOS_TASK_H
#define MYOS_TASK_H

/* Task concept, per spec section 25. This milestone only introduces the
 * task_t struct and a static table of example tasks — there is no
 * scheduler yet (that's Milestone 12) and nothing actually runs these
 * tasks concurrently. task_init() just populates the table so the shell's
 * `tasks` command has something to show. */

#define TASK_STATE_READY 1

typedef struct {
    unsigned char id;
    unsigned char state;
    void (*entry)(void);
} task_t;

/* Populates the fixed-size task table with the spec's example tasks
 * (shell, LED, system service), all starting in TASK_STATE_READY. */
void task_init(void);

/* Number of entries in the task table (fixed size, MAX_TASKS). */
unsigned char task_count(void);

/* Read-only access to a table entry by index. Returns NULL if index is
 * out of range. */
const task_t *task_get(unsigned char index);

/* Example task: blinks the onboard LED (pin 13) roughly every 500 ticks
 * (500ms at the 1kHz tick rate), busy-polling timer_get_ticks(). Correct
 * and complete on its own, but not invoked by anything yet — there is no
 * scheduler to hand control to it until Milestone 12. */
void task_led(void);

/* Example task: reserved for future system-level periodic work (e.g.
 * watchdog petting once Milestone 35 lands). Does nothing yet. */
void task_system_service(void);

#endif /* MYOS_TASK_H */
