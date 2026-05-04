#include "timer.h"
#include <stdio.h>
#include <stdlib.h>
#include "mmu.h"

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
	bool     is_running;
	bool     enabled;
} timer = { .is_running = true };

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
	case TIMER_DIVIDER: timer.time = 0;        return;
	case TIMER_COUNTER: timer.counter = value; return;
	case TIMER_MODULO:  timer.modulo = value;  return;
	case TIMER_CONTROL:
		timer.enabled      = value & (1 << 2);
		timer.clock_select = value & 0x03;
		return;
	default:
		fprintf(stderr, "[ERROR] Write to illegal timer address %04X\n", address);
		exit(EXIT_FAILURE);
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
	if (!timer.enabled)
		return;

	unsigned increment_factor;
	switch (timer.clock_select) {
        case 0x00: increment_factor = 1024; break;
        case 0x01: increment_factor = 16;   break;
        case 0x02: increment_factor = 64;   break;
        case 0x03: increment_factor = 256;  break;
	default:
		fprintf(stderr, "[ERROR] Illegal timer clock_select value %02X\n", timer.clock_select);
		exit(EXIT_FAILURE);
	}

	if (timer.time % increment_factor != 0)
		return;

	if (timer.counter == 0xFF) {
		timer.counter = timer.modulo;
		mmu_set_flag(TIMER_INTERRUPT_FLAG);
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
