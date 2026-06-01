#ifndef JOYPAD_H
#define JOYPAD_H

#include <stdbool.h>
#include <stdint.h>
#include "core_types.h"

typedef enum HagemuButton {
	JOYPAD_BUTTON_RIGHT,
	JOYPAD_BUTTON_LEFT,
	JOYPAD_BUTTON_UP,
	JOYPAD_BUTTON_DOWN,
	JOYPAD_BUTTON_A,
	JOYPAD_BUTTON_B,
	JOYPAD_BUTTON_START,
	JOYPAD_BUTTON_SELECT,
} HagemuButton;

uint8_t joypad_get_byte(void);
void joypad_set_byte(uint8_t byte);
void joypad_set_button(HagemuButton button, bool is_down);

#endif
