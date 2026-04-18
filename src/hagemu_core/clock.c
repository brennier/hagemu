#include "clock.h"
#include "mmu.h"

uint16_t master_clock = 0; // measured in t-cycles
bool clock_running = true;

uint16_t clock_get() {
	return master_clock;
}

void clock_start() {
	clock_running = true;
}

void clock_increment() {
	if (clock_is_running())
		master_clock += 4;

	// Return early if timer control is off
	if (!mmu_get_bit(TIMER_CONTROL_ENABLE_BIT))
		return;

	unsigned increment_factor;
	switch (mmu_read(TIMER_CONTROL) & 0x3) {
        case 0x00: increment_factor = 1024; break;
        case 0x01: increment_factor = 16;   break;
        case 0x02: increment_factor = 64;   break;
        case 0x03: increment_factor = 256;  break;
	}

	if (master_clock % increment_factor != 0)
		return;

	if (mmu_read(TIMER_COUNTER) == 0xFF) {
		mmu_write(TIMER_COUNTER, mmu_read(TIMER_MODULO));
		mmu_set_bit(TIMER_INTERRUPT_FLAG_BIT);
	} else {
		mmu_write(TIMER_COUNTER, mmu_read(TIMER_COUNTER) + 1);
	}
}

void clock_stop() {
	clock_running = false;
}

void clock_reset() {
	master_clock = 0;
}

bool clock_is_running() {
	return clock_running;
}
