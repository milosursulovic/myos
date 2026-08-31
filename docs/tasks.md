# Tasks — Milestone 11

## What it does

`kernel/task.h` / `kernel/task.c` introduce the task concept, per spec
section 25: a `task_t` struct and a fixed-size static table of a few
example tasks. `shell/shell.c` gets a `tasks` command listing the table.

This milestone deliberately stops there. There is **no scheduler** and
**no `task_yield()`** yet — those are Milestone 12 (spec section 26). The
task table exists, but nothing hands control to task 2 or task 3;
`kernel_main()` still calls `shell_run()` directly and it still never
returns, exactly as before this milestone.

## Struct

```c
#define TASK_STATE_READY 1

typedef struct {
    unsigned char id;
    unsigned char state;
    void (*entry)(void);
} task_t;
```

Only one state constant exists so far (`TASK_STATE_READY`). States like
RUNNING/BLOCKED aren't defined yet — nothing needs to distinguish them
until the scheduler exists to actually switch between tasks.

## Task table (`kernel/task.c`)

A fixed-size `static task_t task_table[MAX_TASKS]` (`MAX_TASKS = 3`),
file-local static, mirroring `kernel/memory.c`'s block-list pattern —
never exposed directly, only through accessor functions. `task_init()`
populates it with the spec's example, all starting `TASK_STATE_READY`:

| id | entry                  |
|----|------------------------|
| 1  | `shell_run` (shell.h)  |
| 2  | `task_led`             |
| 3  | `task_system_service`  |

## API

```c
void task_init(void);                          // populates task_table
unsigned char task_count(void);                 // MAX_TASKS
const task_t *task_get(unsigned char index);    // &task_table[index], or NULL if out of range
void task_led(void);                            // example task, see below
void task_system_service(void);                 // example task, see below
```

## `task_led()`

A real, standalone, individually-correct LED blink loop — not a stub. It
busy-polls `timer_get_ticks()` (Milestone 7) and toggles the onboard LED
(pin 13, via `gpio_set`/`gpio_clear`) every 500 ticks (~500ms at the
established 1kHz tick rate):

```c
void task_led(void)
{
    unsigned long last_toggle = timer_get_ticks();
    unsigned char led_on = 0;

    for (;;) {
        unsigned long now = timer_get_ticks();
        if ((unsigned long)(now - last_toggle) >= LED_TOGGLE_TICKS) {
            last_toggle = now;
            led_on = !led_on;
            led_on ? gpio_set(LED_PIN) : gpio_clear(LED_PIN);
        }
    }
}
```

It does **not** call `task_yield()` — that function doesn't exist until
Milestone 12. This function is correct and complete on its own, but
nothing currently calls it: it's registered in the task table (id 2) but
there is no scheduler to invoke it. Milestone 12 will adapt it to
cooperatively yield inside the loop instead of only being callable in
isolation.

## `task_system_service()`

An honestly-labeled placeholder — the spec only names this as the 3rd
example task without defining its behavior yet:

```c
void task_system_service(void)
{
    for (;;) {
        /* Nothing to do yet. */
    }
}
```

Reserved for future system-level periodic work (e.g. watchdog petting once
Milestone 35 lands). It is registered in the task table (id 3) but,
like `task_led`, nothing invokes it yet.

## Kernel wiring (`kernel/kernel.c`)

```c
uart_init();
gpio_init();
timer_init();
sei();
task_init();   // new: populates the task table
shell_run();   // unchanged: still called directly, still never returns
```

`task_init()` only fills in `task_table` so the `tasks` shell command has
something to show. It does not change how the program actually runs.

## Shell command

```
myos> tasks
ID  STATE
1   READY
2   READY
3   READY
```

Reads the table via `task_count()`/`task_get()` only — no invented
per-task name strings, just the struct's actual `id`/`state` fields.

## How to test manually

1. `make flash PORT=<your port>`.
2. Connect a serial terminal at 9600 8N1.
3. Type `tasks` — expect all 3 entries listed as `READY` (ids 1-3).
4. Type `help` — expect `tasks` listed alongside `help`, `info`, `echo`,
   `gpio`, `uptime`, `mem`.
5. General regression check: `info`, `echo`, `gpio`, `uptime`, `mem`
   should all still work as before — this milestone doesn't touch any of
   them.

Note: `task_led`/`task_system_service` are not reachable from the shell
or anywhere else yet (no scheduler exists to call them), so there is
nothing to observe on hardware for those two beyond "the build works and
`tasks` lists them." Real exercise of `task_led`'s blink behavior happens
once Milestone 12 (Scheduler) actually invokes it.

**Status:** hardware-verified (2026-08-31). `tasks` printed exactly the
expected `ID  STATE` table with all 3 entries `READY`; regression-checked
`help`/`info`/`gpio`/`echo`/`uptime`/`mem` all still work correctly.
`task_led`/`task_system_service` remain unexercised (nothing calls them
yet, as expected) — real behavioral testing of those happens once
Milestone 12's scheduler actually runs them.
