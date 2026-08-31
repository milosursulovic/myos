#ifndef MYOS_GPIO_H
#define MYOS_GPIO_H

/* GPIO driver: direct AVR register access for the 14 digital pins on the
 * Arduino Uno (D0-D13), mapped per the real Uno schematic:
 *   D0-D7  -> PORTD, bit = pin
 *   D8-D13 -> PORTB, bit = pin - 8
 *
 * Pins outside 0-13 are invalid. There is no error-reporting mechanism in
 * this API: gpio_set()/gpio_clear() silently no-op (no register is
 * written), and gpio_read() returns 0.
 */

void gpio_init(void);
void gpio_set(unsigned char pin);
void gpio_clear(unsigned char pin);
unsigned char gpio_read(unsigned char pin);

#endif /* MYOS_GPIO_H */
