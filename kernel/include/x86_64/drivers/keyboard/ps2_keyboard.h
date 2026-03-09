#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

uint8_t init_keyboard();
void    keyboard_handler(void);
uint8_t consume_key(void);
#endif