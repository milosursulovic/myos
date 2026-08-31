#include <avr/io.h>
#include "gpio.h"

/* Digital pins D0-D7 live on PORTD (bit = pin number); D8-D13 live on
 * PORTB (bit = pin number - 8). This matches the ATmega328P wiring on the
 * real Arduino Uno board (e.g. D13 = PB5, the built-in LED pin). */

void gpio_init(void)
{
    /* Clear the DDR bits for all 14 managed pins so they start as inputs.
     * This is already the hardware reset default, but it's written
     * explicitly here rather than relied upon, for clarity/correctness. */
    DDRD &= (unsigned char)~0xFF;
    DDRB &= (unsigned char)~0x3F;
}

void gpio_set(unsigned char pin)
{
    if (pin <= 7) {
        DDRD |= (1 << pin);
        PORTD |= (1 << pin);
    } else if (pin <= 13) {
        unsigned char bit = pin - 8;
        DDRB |= (1 << bit);
        PORTB |= (1 << bit);
    }
    /* pin > 13: invalid, no-op — no register is written. */
}

void gpio_clear(unsigned char pin)
{
    if (pin <= 7) {
        DDRD |= (1 << pin);
        PORTD &= (unsigned char)~(1 << pin);
    } else if (pin <= 13) {
        unsigned char bit = pin - 8;
        DDRB |= (1 << bit);
        PORTB &= (unsigned char)~(1 << bit);
    }
    /* pin > 13: invalid, no-op — no register is written. */
}

unsigned char gpio_read(unsigned char pin)
{
    if (pin <= 7) {
        return (PIND & (1 << pin)) ? 1 : 0;
    } else if (pin <= 13) {
        unsigned char bit = pin - 8;
        return (PINB & (1 << bit)) ? 1 : 0;
    }

    /* pin > 13: invalid, return 0. */
    return 0;
}
