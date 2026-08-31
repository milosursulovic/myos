#include <avr/pgmspace.h>
#include "shell.h"
#include "drivers/gpio.h"
#include "drivers/uart.h"
#include "kernel/timer.h"
#include "kernel/memory.h"

/* Fixed-size line buffer. 32 bytes is generous for a shell command plus a
 * short argument on a 2KB-SRAM device without being wasteful; input beyond
 * this bound is silently refused rather than overflowing the buffer. */
#define LINE_MAX 32

#define BACKSPACE 0x08
#define DELETE    0x7F
#define ENTER     '\r'

/* Matches CLAUDE.md's hardware table (ATmega328P: 2048 bytes SRAM). */
#define RAM_TOTAL_BYTES 2048

static char line_buf[LINE_MAX];

/* Compares a NUL-terminated RAM string against a NUL-terminated flash
 * string (PSTR literal). Avoids pulling in avr-libc's strcmp, which isn't
 * linked (Makefile uses -nodefaultlibs). Returns 1 if equal, 0 otherwise. */
static unsigned char str_eq_p(const char *ram, const char *pgm)
{
    unsigned char rc, pc;

    do {
        rc = (unsigned char)*ram++;
        pc = pgm_read_byte(pgm++);
        if (rc != pc) {
            return 0;
        }
    } while (rc);

    return 1;
}

static void print_prompt(void)
{
    uart_puts_P(PSTR("myos>"));
}

static void print_banner(void)
{
    uart_puts_P(PSTR("====================================\r\n"));
    uart_puts_P(PSTR("        MyOS v0.1\r\n"));
    uart_puts_P(PSTR("====================================\r\n"));
    uart_puts_P(PSTR("\r\n"));
    uart_puts_P(PSTR("ATmega328P kernel started.\r\n"));
    uart_puts_P(PSTR("\r\n"));
}

static void cmd_help(void)
{
    uart_puts_P(PSTR("help\r\n"));
    uart_puts_P(PSTR("info\r\n"));
    uart_puts_P(PSTR("echo\r\n"));
    uart_puts_P(PSTR("gpio\r\n"));
    uart_puts_P(PSTR("uptime\r\n"));
    uart_puts_P(PSTR("mem\r\n"));
}

static void cmd_info(void)
{
    uart_puts_P(PSTR("OS:      MyOS\r\n"));
    uart_puts_P(PSTR("Version: 0.1\r\n"));
    uart_puts_P(PSTR("CPU:     ATmega328P\r\n"));
    uart_puts_P(PSTR("Clock:   16 MHz\r\n"));
    uart_puts_P(PSTR("Flash:   32768 bytes\r\n"));
    uart_puts_P(PSTR("RAM:     2048 bytes\r\n"));
    uart_puts_P(PSTR("EEPROM:  1024 bytes\r\n"));
}

static void cmd_echo(const char *args)
{
    uart_puts(args);
    uart_puts_P(PSTR("\r\n"));
}

static void cmd_gpio_usage(void)
{
    uart_puts_P(PSTR("Usage: gpio <pin> on|off\r\n"));
}

/* Parses "<pin> on" / "<pin> off" out of args (pin is decimal digits only,
 * no strtol available) and drives the pin accordingly. Anything malformed
 * (non-numeric or out-of-range pin, missing/unrecognized mode token)
 * prints a usage message instead of guessing. */
static void cmd_gpio(const char *args)
{
    unsigned int pin = 0;
    unsigned char has_digit = 0;

    while (*args == ' ') {
        args++;
    }

    while (*args >= '0' && *args <= '9') {
        pin = (unsigned int)(pin * 10 + (unsigned char)(*args - '0'));
        has_digit = 1;
        args++;
        if (pin > 13) {
            break;
        }
    }

    while (*args == ' ') {
        args++;
    }

    if (!has_digit || pin > 13 || *args == '\0') {
        cmd_gpio_usage();
        return;
    }

    if (str_eq_p(args, PSTR("on"))) {
        gpio_set((unsigned char)pin);
        uart_puts_P(PSTR("GPIO "));
        uart_put_uint(pin);
        uart_puts_P(PSTR(": ON\r\n"));
    } else if (str_eq_p(args, PSTR("off"))) {
        gpio_clear((unsigned char)pin);
        uart_puts_P(PSTR("GPIO "));
        uart_put_uint(pin);
        uart_puts_P(PSTR(": OFF\r\n"));
    } else {
        cmd_gpio_usage();
    }
}

static void cmd_uptime(void)
{
    unsigned long ticks = timer_get_ticks();

    uart_puts_P(PSTR("Ticks: "));
    uart_put_ulong(ticks);
    uart_puts_P(PSTR("\r\n"));

    uart_puts_P(PSTR("Uptime: "));
    uart_put_ulong(ticks / 1000);
    uart_puts_P(PSTR(" seconds\r\n"));
}

/* Free/used are derived from kmem_free_bytes() so they always sum to
 * exactly RAM_TOTAL_BYTES, matching the spec's own example (320 + 1728 =
 * 2048) — there's no separately-tracked "sum of allocated bytes" that
 * could drift out of sync with the allocator's own notion of free space. */
static void cmd_mem(void)
{
    unsigned int free_bytes = kmem_free_bytes();
    unsigned int used_bytes = RAM_TOTAL_BYTES - free_bytes;

    uart_puts_P(PSTR("RAM total: "));
    uart_put_uint(RAM_TOTAL_BYTES);
    uart_puts_P(PSTR("\r\n"));

    uart_puts_P(PSTR("Used:      "));
    uart_put_uint(used_bytes);
    uart_puts_P(PSTR("\r\n"));

    uart_puts_P(PSTR("Free:      "));
    uart_put_uint(free_bytes);
    uart_puts_P(PSTR("\r\n"));
}

static void cmd_unknown(const char *name)
{
    uart_puts_P(PSTR("Unknown command: "));
    uart_puts(name);
    uart_puts_P(PSTR("\r\n"));
}

/* Splits line_buf in place into a command token and an argument string,
 * then dispatches to the matching handler. */
static void dispatch(char *line)
{
    while (*line == ' ') {
        line++;
    }

    char *cmd = line;
    char *args = line;

    while (*args && *args != ' ') {
        args++;
    }
    if (*args == ' ') {
        *args++ = '\0';
        while (*args == ' ') {
            args++;
        }
    }

    if (*cmd == '\0') {
        return;
    }

    if (str_eq_p(cmd, PSTR("help"))) {
        cmd_help();
    } else if (str_eq_p(cmd, PSTR("info"))) {
        cmd_info();
    } else if (str_eq_p(cmd, PSTR("echo"))) {
        cmd_echo(args);
    } else if (str_eq_p(cmd, PSTR("gpio"))) {
        cmd_gpio(args);
    } else if (str_eq_p(cmd, PSTR("uptime"))) {
        cmd_uptime();
    } else if (str_eq_p(cmd, PSTR("mem"))) {
        cmd_mem();
    } else {
        cmd_unknown(cmd);
    }
}

/* Reads one line from the UART into line_buf, handling backspace/delete
 * editing and terminating on ENTER ('\r'). Bare '\n' is ignored so a
 * '\r\n' pair sent by a terminal doesn't trigger the line handler twice. */
static void read_line(void)
{
    unsigned char len = 0;

    for (;;) {
        char c = uart_getc();

        if (c == ENTER) {
            line_buf[len] = '\0';
            uart_puts_P(PSTR("\r\n"));
            return;
        }

        if (c == '\n') {
            continue;
        }

        if (c == BACKSPACE || c == DELETE) {
            if (len > 0) {
                len--;
                uart_puts_P(PSTR("\b \b"));
            }
            continue;
        }

        if (len < LINE_MAX - 1) {
            line_buf[len++] = c;
            uart_putc(c);
        }
    }
}

void shell_run(void)
{
    print_banner();

    for (;;) {
        print_prompt();
        read_line();
        dispatch(line_buf);
    }
}
