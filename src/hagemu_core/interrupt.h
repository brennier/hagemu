#ifndef HAGEMU_INTERRUPT_H
#define HAGEMU_INTERRUPT_H

#include <stdint.h>
#include <stdbool.h>

enum HagemuInterruptFlag {
	VBLANK_INTERRUPT,
	LCD_INTERRUPT,
	TIMER_INTERRUPT,
	SERIAL_INTERRUPT,
	JOYPAD_INTERRUPT,
};

struct HagemuInterrupts {
	bool vblank;
	bool lcd;
	bool timer;
	bool serial;
	bool joypad;
	uint8_t enabled_register;
};

void interrupt_reset(struct HagemuInterrupts *interrupts);
void interrupt_raise(struct HagemuInterrupts *interrupts, enum HagemuInterruptFlag flag);
void interrupt_clear(struct HagemuInterrupts *interrupts, enum HagemuInterruptFlag flag);
uint8_t interrupt_register_read(struct HagemuInterrupts *interrupts);
void interrupt_register_write(struct HagemuInterrupts *interrupts, uint8_t value);
bool interrupt_pending(struct HagemuInterrupts *interrupts);
enum HagemuInterruptFlag interrupt_get_next(struct HagemuInterrupts *interrupts);
uint8_t interrupt_enable_register_read(struct HagemuInterrupts *interrupts);
void interrupt_enable_register_write(struct HagemuInterrupts *interrupts, uint8_t value);

#endif
