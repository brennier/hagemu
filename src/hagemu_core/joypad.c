#include <stdio.h>
#include "joypad.h"
#include "interrupt.h"

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
} joypad = { 0 };

void joypad_set_button(HagemuButton button, bool is_down) {
	bool *target = NULL;
	switch (button) {
	case JOYPAD_BUTTON_RIGHT:  target = &joypad.right; break;
	case JOYPAD_BUTTON_LEFT:   target = &joypad.left; break;
	case JOYPAD_BUTTON_UP:     target = &joypad.up; break;
	case JOYPAD_BUTTON_DOWN:   target = &joypad.down; break;
	case JOYPAD_BUTTON_A:      target = &joypad.a; break;
	case JOYPAD_BUTTON_B:      target = &joypad.b; break;
	case JOYPAD_BUTTON_SELECT: target = &joypad.select; break;
	case JOYPAD_BUTTON_START:  target = &joypad.start; break;
	}

	if (is_down && *target == false)
		interrupt_raise(JOYPAD_INTERRUPT);

	*target = is_down;
}

void joypad_set_byte(uint8_t byte) {
	// Only bytes 4 and 5 are writable. The rest are ignored.
	joypad.select_dpad    = !((byte >> 4) & 0x01);
	joypad.select_buttons = !((byte >> 5) & 0x01);
}

uint8_t joypad_get_byte(void) {
	uint8_t joypad_byte = 0x00;

	joypad_byte |= (joypad.select_buttons) << 5;
	joypad_byte |= (joypad.select_dpad)    << 4;
	joypad_byte |= (joypad.select_dpad    && joypad.down)   << 3;
	joypad_byte |= (joypad.select_buttons && joypad.start)  << 3;
	joypad_byte |= (joypad.select_dpad    && joypad.up)     << 2;
	joypad_byte |= (joypad.select_buttons && joypad.select) << 2;
	joypad_byte |= (joypad.select_dpad    && joypad.left)   << 1;
	joypad_byte |= (joypad.select_buttons && joypad.b)      << 1;
	joypad_byte |= (joypad.select_dpad    && joypad.right)  << 0;
	joypad_byte |= (joypad.select_buttons && joypad.a)      << 0;

	joypad_byte = ~joypad_byte;
	return joypad_byte;
}
