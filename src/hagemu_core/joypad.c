#include <stdio.h>
#include "joypad.h"
#include "mmu.h"

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
	switch (button) {
	case HAGEMU_BUTTON_RIGHT:  joypad.right  = is_down; break;
	case HAGEMU_BUTTON_LEFT:   joypad.left   = is_down; break;
	case HAGEMU_BUTTON_UP:     joypad.up     = is_down; break;
	case HAGEMU_BUTTON_DOWN:   joypad.down   = is_down; break;
	case HAGEMU_BUTTON_A:      joypad.a      = is_down; break;
	case HAGEMU_BUTTON_B:      joypad.b      = is_down; break;
	case HAGEMU_BUTTON_SELECT: joypad.select = is_down; break;
	case HAGEMU_BUTTON_START:  joypad.start  = is_down; break;
	}

	if (is_down)
		mmu_set_flag(JOYPAD_INTERRUPT_FLAG);
}

void joypad_set_byte(uint8_t byte) {
	// Only bytes 4 and 5 are writable. The rest are ignored.
	joypad.select_dpad    = !((byte >> 4) & 0x01);
	joypad.select_buttons = !((byte >> 5) & 0x01);
}

uint8_t joypad_get_byte() {
	uint8_t joypad_byte = 0x00;

	joypad_byte |= (joypad.select_dpad)    << 5;
	joypad_byte |= (joypad.select_buttons) << 4;
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
