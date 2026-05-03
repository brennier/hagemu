#include "timer.h"
#include "mmu.h"

struct HagemuTimer {
	uint16_t time; // measured in t-cycles
	bool is_running;
} timer = { .is_running = true };

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
	if (!mmu_get_bit(TIMER_CONTROL_ENABLE_BIT))
		return;

	unsigned increment_factor;
	switch (mmu_read(TIMER_CONTROL) & 0x3) {
        case 0x00: increment_factor = 1024; break;
        case 0x01: increment_factor = 16;   break;
        case 0x02: increment_factor = 64;   break;
        case 0x03: increment_factor = 256;  break;
	}

	if (timer.time % increment_factor != 0)
		return;

	if (mmu_read(TIMER_COUNTER) == 0xFF) {
		mmu_write(TIMER_COUNTER, mmu_read(TIMER_MODULO));
		mmu_set_bit(TIMER_INTERRUPT_FLAG_BIT);
	} else {
		mmu_write(TIMER_COUNTER, mmu_read(TIMER_COUNTER) + 1);
	}
}

void timer_stop() {
	timer.is_running = false;
}

void timer_reset() {
	timer.time = 0;
}
