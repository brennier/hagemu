#include "interrupt.h"
#include "core.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void interrupt_reset(struct HagemuInterrupts *interrupts) {
	memset(interrupts, 0, sizeof(struct HagemuInterrupts));
}

void interrupt_raise(struct HagemuInterrupts *interrupts, enum HagemuInterruptFlag flag) {
	switch (flag) {
	case VBLANK_INTERRUPT: interrupts->vblank = true; break;
	case LCD_INTERRUPT:    interrupts->lcd    = true; break;
	case TIMER_INTERRUPT:  interrupts->timer  = true; break;
	case SERIAL_INTERRUPT: interrupts->serial = true; break;
	case JOYPAD_INTERRUPT: interrupts->joypad = true; break;
	}
}

void interrupt_clear(struct HagemuInterrupts *interrupts, enum HagemuInterruptFlag flag) {
	switch (flag) {
	case VBLANK_INTERRUPT: interrupts->vblank = false; break;
	case LCD_INTERRUPT:    interrupts->lcd    = false; break;
	case TIMER_INTERRUPT:  interrupts->timer  = false; break;
	case SERIAL_INTERRUPT: interrupts->serial = false; break;
	case JOYPAD_INTERRUPT: interrupts->joypad = false; break;
	}
}

uint8_t interrupt_register_read(struct HagemuInterrupts *interrupts) {
	uint8_t value = 0xE0;
	value |= (interrupts->vblank << 0);
	value |= (interrupts->lcd    << 1);
	value |= (interrupts->timer  << 2);
	value |= (interrupts->serial << 3);
	value |= (interrupts->joypad << 4);
	return value;
}

void interrupt_register_write(struct HagemuInterrupts *interrupts, uint8_t value) {
	interrupts->vblank = value & (1 << 0);
	interrupts->lcd    = value & (1 << 1);
	interrupts->timer  = value & (1 << 2);
	interrupts->serial = value & (1 << 3);
	interrupts->joypad = value & (1 << 4);
}

uint8_t interrupt_enable_register_read(struct HagemuInterrupts *interrupts) {
	return interrupts->enabled_register;
}

void interrupt_enable_register_write(struct HagemuInterrupts *interrupts, uint8_t value) {
	interrupts->enabled_register = value;
}

bool interrupt_pending(struct HagemuInterrupts *interrupts) {
	// Only check the lower 5 bits
	return interrupt_register_read(interrupts) & interrupts->enabled_register & 0x1F;
}

enum HagemuInterruptFlag interrupt_get_next(struct HagemuInterrupts *interrupts) {
	uint8_t interrupt_reg = interrupt_register_read(interrupts) & interrupts->enabled_register;
	if (interrupt_reg & 0x01)
		return VBLANK_INTERRUPT;
	else if (interrupt_reg & 0x02)
		return LCD_INTERRUPT;
	else if (interrupt_reg & 0x04)
		return TIMER_INTERRUPT;
	else if (interrupt_reg & 0x08)
		return SERIAL_INTERRUPT;
	else if (interrupt_reg & 0x10)
		return JOYPAD_INTERRUPT;

	printf("interrupt_get_next was called, but there were no pending interrupts\n");
	exit(EXIT_FAILURE);
}
