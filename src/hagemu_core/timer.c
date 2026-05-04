#include "timer.h"
#include "mmu.h"

#define TIMER_DIVIDER 0xFF04
#define TIMER_COUNTER 0xFF05
#define TIMER_MODULO  0xFF06
#define TIMER_CONTROL 0xFF07

struct HagemuTimer {
	uint16_t time; // measured in t-cycles
	uint8_t control;
	uint8_t divider;
	uint8_t modulo;
	uint8_t counter;
	bool is_running;
} timer = { .is_running = true };

uint8_t timer_register_read(uint16_t address) {
	switch (address) {
	case TIMER_DIVIDER:
		// get the time from timer.h
		return ((timer.time & 0xFF00) >> 8);
	case TIMER_COUNTER: return timer.counter;
	case TIMER_MODULO: return timer.modulo;
	case TIMER_CONTROL:
		// bits 3 through 7 should always be 1
		return timer.control | 0xF8;
	}
}

void timer_register_write(uint16_t address, uint8_t value) {
	switch(address) {
	case TIMER_DIVIDER:
		timer.time = 0;
		return;
	case TIMER_COUNTER: timer.counter = value; return;
	case TIMER_MODULO:  timer.modulo = value; return;
	case TIMER_CONTROL:
		value &= 0x07; // Mask all but the lowest 3 bits
		timer.control = value;
		return;
	}
}

uint16_t timer_get() {
	return timer.time;
}

void timer_start() {
	timer.is_running = true;
}

void timer_tick() {
	if (timer.is_running)
		timer.time += 4;

	// Return early if timer control is off
	if (!(timer.control & (1 << 2)))
		return;

	unsigned increment_factor;
	switch (timer.control & 0x3) {
        case 0x00: increment_factor = 1024; break;
        case 0x01: increment_factor = 16;   break;
        case 0x02: increment_factor = 64;   break;
        case 0x03: increment_factor = 256;  break;
	}

	if (timer.time % increment_factor != 0)
		return;

	if (timer.counter == 0xFF) {
		timer.counter = timer.modulo;
		mmu_set_bit(TIMER_INTERRUPT_FLAG_BIT);
	} else {
		timer.counter++;
	}
}

void timer_stop() {
	timer.is_running = false;
}

void timer_reset() {
	timer.time = 0;
}
