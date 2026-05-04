#ifndef HAGEMU_INTERRUPT_H
#define HAGEMU_INTERRUPT_H

#include <stdint.h>

enum HagemuInterruptFlag {
	VBLANK_INTERRUPT,
	LCD_INTERRUPT,
	TIMER_INTERRUPT,
	SERIAL_INTERRUPT,
	JOYPAD_INTERRUPT,
};

void interrupt_reset();
void interrupt_raise(enum HagemuInterruptFlag flag);
void interrupt_clear(enum HagemuInterruptFlag flag);
uint8_t interrupt_register_read();
void interrupt_register_write(uint8_t value);

#endif
