#ifndef MYOS_SHELL_H
#define MYOS_SHELL_H

/* Command-line shell: reads a line over UART with backspace editing,
 * dispatches it on ENTER to a fixed command table, and re-prompts.
 * Never returns. */

void shell_run(void);

#endif /* MYOS_SHELL_H */
