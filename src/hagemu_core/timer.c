#include "timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "interrupt.h"

#define TIMER_DIVIDER 0xFF04
#define TIMER_COUNTER 0xFF05
#define TIMER_MODULO  0xFF06
#define TIMER_CONTROL 0xFF07

struct HagemuTimer {
	uint16_t time; // measured in t-cycles
	uint16_t clock_select;
	uint8_t  timer_control_raw;
	uint8_t  divider;
	uint8_t  modulo;
	uint8_t  counter;
	bool     enabled;
	bool     double_speed_mode;
} timer = { 0 };

static void set_clock_select(void) {
	uint8_t select = timer.timer_control_raw & 0x03;
	timer.clock_select = 1;
	switch (select) {
        case 0x00: timer.clock_select <<= 9; break;
        case 0x01: timer.clock_select <<= 3; break;
        case 0x02: timer.clock_select <<= 5; break;
        case 0x03: timer.clock_select <<= 7; break;
	}
	if (timer.double_speed_mode)
		timer.clock_select <<= 1;
}

static void maybe_increment(uint16_t old_time, uint16_t new_time) {
	// Return early if timer control is off
	if (!timer.enabled)
		return;

	if ((old_time & timer.clock_select) == 0)
		return;
	if ((new_time & timer.clock_select) != 0)
		return;

	if (timer.counter == 0xFF) {
		timer.counter = timer.modulo;
		interrupt_raise(TIMER_INTERRUPT);
	} else {
		timer.counter++;
	}
}

uint8_t timer_register_read(uint16_t address) {
	switch (address) {
	case TIMER_DIVIDER: return timer.time >> 8;
	case TIMER_COUNTER: return timer.counter;
	case TIMER_MODULO:  return timer.modulo;
	case TIMER_CONTROL: return timer.timer_control_raw | 0xF8;
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
		timer.timer_control_raw = value;
		timer.enabled = value & (1 << 2);
		set_clock_select();
		return;
	default:
		fprintf(stderr, "[ERROR] Write to illegal timer address %04X\n", address);
		exit(EXIT_FAILURE);
	}
}

void timer_set_speed_mode(bool double_speed_mode) {
	timer.double_speed_mode = double_speed_mode;
	maybe_increment(timer.time, 0);
	timer.time = 0;
	set_clock_select();
}

void timer_tick(int t_cycles) {
	maybe_increment(timer.time, timer.time + t_cycles);
	timer.time += t_cycles;
}

void timer_reset(void) {
	memset(&timer, 0, sizeof(struct HagemuTimer));
}
