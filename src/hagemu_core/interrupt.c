#include "interrupt.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

struct HagemuInterrupts {
	bool vblank;
	bool lcd;
	bool timer;
	bool serial;
	bool joypad;
	uint8_t enabled_register;
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

uint8_t interrupt_enable_register_read() {
	return interrupt.enabled_register;
}

void interrupt_enable_register_write(uint8_t value) {
	interrupt.enabled_register = value;
}

bool interrupt_pending() {
	return interrupt_register_read() & interrupt.enabled_register;
}

enum HagemuInterruptFlag interrupt_get_next() {
	uint8_t interrupts = interrupt_register_read() & interrupt.enabled_register;
	if (interrupts & 0x01)
		return VBLANK_INTERRUPT;
	else if (interrupts & 0x02)
		return LCD_INTERRUPT;
	else if (interrupts & 0x04)
		return TIMER_INTERRUPT;
	else if (interrupts & 0x08)
		return SERIAL_INTERRUPT;
	else if (interrupts & 0x10)
		return JOYPAD_INTERRUPT;

	printf("Interrupt_get was called, but there were no pending interrupts\n");
	exit(EXIT_FAILURE);
}
