#include "timer.h"
#include <stdio.h>
#include <stdlib.h>
#include "interrupt.h"

#define TIMER_DIVIDER 0xFF04
#define TIMER_COUNTER 0xFF05
#define TIMER_MODULO  0xFF06
#define TIMER_CONTROL 0xFF07

struct HagemuTimer {
	uint16_t time; // measured in t-cycles
	uint8_t  divider;
	uint8_t  modulo;
	uint8_t  counter;
	uint8_t  clock_select;
	bool     enabled;
} timer = { 0 };

void maybe_increment(uint16_t old_time, uint16_t new_time) {
	// Return early if timer control is off
	if (!timer.enabled)
		return;

	uint16_t bit = 1;
	switch (timer.clock_select) {
        case 0x00: bit <<= 9; break;
        case 0x01: bit <<= 3; break;
        case 0x02: bit <<= 5; break;
        case 0x03: bit <<= 7; break;
	default:
		fprintf(stderr, "[ERROR] Illegal timer clock_select value %02X\n", timer.clock_select);
		exit(EXIT_FAILURE);
	}

	if ((old_time & bit) != 0 && (new_time & bit) == 0) {
		if (timer.counter == 0xFF) {
			timer.counter = timer.modulo;
			interrupt_raise(TIMER_INTERRUPT);
		} else {
			timer.counter++;
		}
	}
}

uint8_t timer_register_read(uint16_t address) {
	switch (address) {
	case TIMER_DIVIDER: return timer.time >> 8;
	case TIMER_COUNTER: return timer.counter;
	case TIMER_MODULO:  return timer.modulo;
	case TIMER_CONTROL: return 0xF8 | (timer.enabled << 2) | timer.clock_select;
	default:
		fprintf(stderr, "[ERROR] Read from illegal timer address %04X\n", address);
		exit(EXIT_FAILURE);
	}
}

void timer_register_write(uint16_t address, uint8_t value) {
	switch(address) {
	case TIMER_DIVIDER:
		maybe_increment(timer.time, 0);
		timer.time = 0;
		return;
	case TIMER_COUNTER:
		timer.counter = value;
		return;
	case TIMER_MODULO:
		timer.modulo = value;
		return;
	case TIMER_CONTROL:
		timer.enabled      = value & (1 << 2);
		timer.clock_select = value & 0x03;
		return;
	default:
		fprintf(stderr, "[ERROR] Write to illegal timer address %04X\n", address);
		exit(EXIT_FAILURE);
	}
}

void timer_tick(void) {
	for (int i = 0; i < 4; i++) {
		timer.time++;
		maybe_increment(timer.time-1, timer.time);
	}
}
