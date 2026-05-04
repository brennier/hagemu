#include "interrupt.h"
#include <stdbool.h>
#include <string.h>

struct HagemuInterrupts {
	bool vblank;
	bool lcd;
	bool timer;
	bool serial;
	bool joypad;
} interrupt = { 0 };

void interrupt_reset() {
	memset(&interrupt, 0, sizeof(struct HagemuInterrupts));
}

void interrupt_raise(enum HagemuInterruptFlag flag) {
	switch (flag) {
	case VBLANK_INTERRUPT: interrupt.vblank = true; break;
	case LCD_INTERRUPT:    interrupt.lcd    = true; break;
	case TIMER_INTERRUPT:  interrupt.timer  = true; break;
	case SERIAL_INTERRUPT: interrupt.serial = true; break;
	case JOYPAD_INTERRUPT: interrupt.joypad = true; break;
	}
}

void interrupt_clear(enum HagemuInterruptFlag flag) {
	switch (flag) {
	case VBLANK_INTERRUPT: interrupt.vblank = false; break;
	case LCD_INTERRUPT:    interrupt.lcd    = false; break;
	case TIMER_INTERRUPT:  interrupt.timer  = false; break;
	case SERIAL_INTERRUPT: interrupt.serial = false; break;
	case JOYPAD_INTERRUPT: interrupt.joypad = false; break;
	}
}

uint8_t interrupt_register_read() {
	uint8_t value = 0xE0;
	value |= (interrupt.vblank << 0);
	value |= (interrupt.lcd    << 1);
	value |= (interrupt.timer  << 2);
	value |= (interrupt.serial << 3);
	value |= (interrupt.joypad << 4);
	return value;
}

void interrupt_register_write(uint8_t value) {
	interrupt.vblank = value & (1 << 0);
	interrupt.lcd    = value & (1 << 1);
	interrupt.timer  = value & (1 << 2);
	interrupt.serial = value & (1 << 3);
	interrupt.joypad = value & (1 << 4);
}
