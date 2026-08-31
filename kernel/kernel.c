#include "drivers/uart.h"
#include "shell/shell.h"

void kernel_main(void)
{
    uart_init();
    shell_run();
}
