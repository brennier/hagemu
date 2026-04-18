#ifndef JOYPAD_H
#define JOYPAD_H

#include <stdbool.h>
#include <stdint.h>
#include "core_types.h"

uint8_t joypad_get_byte(void);
void joypad_set_byte(uint8_t byte);
void joypad_set_button(HagemuButton button, bool is_down);

#endif
