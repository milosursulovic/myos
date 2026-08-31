#include "drivers/gpio.h"
#include "drivers/uart.h"
#include "shell/shell.h"

void kernel_main(void)
{
    uart_init();
    gpio_init();
    shell_run();
}
