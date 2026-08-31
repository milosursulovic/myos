#ifndef MYOS_TASK_H
#define MYOS_TASK_H

/* Task concept, per spec section 25, extended in Milestone 12
 * (Scheduler) with genuine cooperative multitasking. task_init()
 * populates a fixed-size table so the shell's `tasks` command has
 * something to show, and kernel/scheduler.c + kernel/context_switch.S
 * actually switch between them via task_yield(). */

#define TASK_STATE_READY 1
/* Set on whichever task is currently executing (kernel/scheduler.c);
 * every other task sits at TASK_STATE_READY between switches -- this is
 * a purely cooperative scheduler, so "not running" always means "ready
 * to run again", never "blocked". */
#define TASK_STATE_RUNNING 2

typedef struct {
    unsigned char id;
    unsigned char state;
    void (*entry)(void);
    /* Saved stack pointer while this task is NOT running. Task 1 (the
     * shell) never has this read/written -- it always runs on the
     * existing boot stack. Task 2/3 get theirs constructed by
     * scheduler_init() and updated on every task_yield() switch-out; see
     * docs/scheduler.md. NEW in Milestone 12 -- a task cannot be
     * suspended and later resumed without somewhere to save this. */
    void *sp;
} task_t;

/* Populates the fixed-size task table with the spec's example tasks
 * (shell, LED, system service), all starting in TASK_STATE_READY. */
void task_init(void);

/* Number of entries in the task table (fixed size, MAX_TASKS). */
unsigned char task_count(void);

/* Access to a table entry by index. Returns NULL if index is out of
 * range. Non-const (as of Milestone 12) since the scheduler needs to
 * mutate a task's sp/state through it; shell.c's read-only usage is
 * unaffected by dropping const. */
task_t *task_get(unsigned char index);

/* Example task: blinks the onboard LED (pin 13) roughly every 500 ticks
 * (500ms at the 1kHz tick rate), busy-polling timer_get_ticks(). Calls
 * task_yield() on iterations where it isn't yet time to toggle, so other
 * tasks (the shell) get the CPU while it waits. */
void task_led(void);

/* Example task: reserved for future system-level periodic work (e.g.
 * watchdog petting once Milestone 35 lands). Does nothing yet beyond
 * immediately yielding every iteration. */
void task_system_service(void);

#endif /* MYOS_TASK_H */
