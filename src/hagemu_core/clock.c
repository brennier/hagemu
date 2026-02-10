#include "clock.h"

uint16_t master_clock = 0; // measured in t-cycles
bool clock_running = true;

void clock_reset() {
	master_clock = 0;
}

uint16_t clock_get() {
	return master_clock;
}

void clock_stop() {
	clock_running = false;
}

void clock_update(int t_cycles) {
	master_clock += t_cycles;
}

void clock_start() {
	clock_running = true;
}

bool clock_is_running() {
	return clock_running;
}