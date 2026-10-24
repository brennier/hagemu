#ifndef HAGEMU_TIMER_H
#define HAGEMU_TIMER_H

#include <stdint.h>
#include <stdbool.h>

struct HagemuTimer {
	uint16_t time; // measured in t-cycles
	uint16_t clock_select;
	uint8_t  timer_control_raw;
	uint8_t  divider;
	uint8_t  modulo;
	uint8_t  counter;
	bool     enabled;
	bool     overflow_pending;
	bool     just_reloaded;
};

void timer_tick(struct HagemuTimer *timer, int t_cycles);
uint8_t timer_register_read(struct HagemuTimer *timer, uint16_t address);
void timer_register_write(struct HagemuTimer *timer, uint16_t address, uint8_t value);
void timer_div_reset(struct HagemuTimer *timer);
void timer_reset(struct HagemuTimer *timer);

#endif
