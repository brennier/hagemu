#ifndef JOYPAD_H
#define JOYPAD_H

#include <stdbool.h>
#include <stdint.h>

struct HagemuJoypad {
	bool select_dpad;
	bool select_buttons;

	bool right;
	bool left;
	bool up;
	bool down;
	bool a;
	bool b;
	bool select;
	bool start;
};

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

void joypad_reset(struct HagemuJoypad *joypad);
uint8_t joypad_get_byte(struct HagemuJoypad *joypad);
void joypad_set_byte(struct HagemuJoypad *joypad, uint8_t byte);
void joypad_set_button(struct HagemuJoypad *joypad, HagemuButton button, bool is_down);

#endif
